#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "include/ext2_dir.h"
#include "include/ext2_file.h"
#include "../../lib/string.h"

static struct ext2_handle g_handles[EXT2_MAX_OPEN];

void ext2_file_handles_reset(void) {
    memset(g_handles, 0, sizeof(g_handles));
}

int32_t ext2_file_open(const char *path) {
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0) {
        return st;
    }
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) {
        return -1;
    }
    uint16_t mode = ext2_read_u16(ib);
    if ((mode & 0xF000) == EXT2_S_IFDIR) {
        return -6;
    }
    for (int i = 0; i < EXT2_MAX_OPEN; i++) {
        if (!g_handles[i].used) {
            g_handles[i].used = true;
            g_handles[i].inode = ino;
            g_handles[i].size = ext2_read_u32(ib + 4);
            g_handles[i].position = 0;
            return EXT2_DESCRIPTOR_BASE + i;
        }
    }
    return -4;
}

int32_t ext2_file_read(int32_t descriptor, void *buffer, uint32_t count) {
    int idx = descriptor - EXT2_DESCRIPTOR_BASE;
    if (idx < 0 || idx >= EXT2_MAX_OPEN || !g_handles[idx].used) {
        return -3;
    }
    if (!buffer && count) {
        return -3;
    }
    struct ext2_handle *h = &g_handles[idx];
    if (h->position >= h->size) {
        return 0;
    }
    if (h->position + count > h->size) {
        count = h->size - h->position;
    }
    uint8_t ib[256];
    if (!ext2_inode_read(h->inode, ib)) {
        return -1;
    }
    int32_t got = ext2_inode_read_data(ib, h->position, buffer, count);
    if (got > 0) {
        h->position += (uint32_t)got;
    }
    return got;
}

static uint32_t g_blocks[8192];

static int32_t ext2_write_data(uint32_t ino, const uint8_t *data, uint32_t size) {
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    uint32_t bcnt = (size + 1023) / 1024;
    if (bcnt > 8192) return -4;
    uint32_t *blocks = g_blocks;
    for (uint32_t i=0;i<bcnt;i++) { blocks[i]=ext2_alloc_block(); if(!blocks[i]) return -4; }
    for (uint32_t i=0;i<bcnt;i++) {
        uint8_t tmp[1024]={0};
        uint32_t off=i*1024; uint32_t left=size-off; uint32_t cur=left>1024?1024:left;
        memcpy(tmp, data+off, cur);
        if(!ext2_write_block(blocks[i], tmp)) return -1;
    }
    memset(ib+40,0,60);
    for(uint32_t i=0;i<bcnt && i<12;i++) ext2_write_u32(ib+40+i*4, blocks[i]);
    if(bcnt>12){
        uint32_t ind=ext2_alloc_block();
        if(!ind) return -4;
        uint8_t ibuf[1024]={0};
        for(uint32_t i=12;i<bcnt && i<12+256;i++) ext2_write_u32(ibuf+(i-12)*4, blocks[i]);
        if(!ext2_write_block(ind, ibuf)) return -1;
        ext2_write_u32(ib+40+12*4, ind);
        if(bcnt>12+256){
            uint32_t dind=ext2_alloc_block();
            if(!dind) return -4;
            uint8_t dbuf[1024]={0};
            uint32_t remain=bcnt-12-256;
            uint32_t off=12+256;
            uint32_t needed=(remain+255)/256;
            for(uint32_t j=0;j<needed;j++){
                uint32_t iblk=ext2_alloc_block();
                if(!iblk) return -4;
                uint8_t sub[1024]={0};
                for(uint32_t k=0;k<256 && off<bcnt;k++,off++) ext2_write_u32(sub+k*4, blocks[off]);
                if(!ext2_write_block(iblk, sub)) return -1;
                ext2_write_u32(dbuf+j*4, iblk);
            }
            if(!ext2_write_block(dind, dbuf)) return -1;
            ext2_write_u32(ib+40+13*4, dind);
        }
    }
    ext2_write_u32(ib+4, size);
    ext2_write_u32(ib+28, bcnt*2);
    ext2_write_u16(ib+24, 1);
    ext2_write_u16(ib, 0x81A4);
    return ext2_write_inode(ino, ib) ? (int32_t)size : -1;
}

static int32_t ext2_create_file_internal(const char *path) {
    const char *p=path;
    while(*p=='/') p++;
    if(!*p) return -3;
    char parent[256]={0}, base[64]={0};
    const char *slash=0;
    for(const char *t=p; *t; t++) if(*t=='/') slash=t;
    if(slash){
        uint32_t plen=slash-p;
        if(plen>=sizeof(parent)) return -3;
        memcpy(parent, "/",1);
        memcpy(parent+1, p, plen);
        parent[plen+1]='\0';
        const char *b=slash+1;
        uint32_t blen=strlen(b);
        if(blen>=sizeof(base)) return -3;
        memcpy(base,b,blen);
    } else {
        parent[0]='/'; base[0]='\0';
        uint32_t blen=strlen(p);
        if(blen>=sizeof(base)) return -3;
        memcpy(base,p,blen);
    }
    // actually simplify: use dir_resolve for parent
    uint32_t par_ino;
    char pardir[256];
    if(slash){
        uint32_t l=slash-p;
        pardir[0]='/';
        memcpy(pardir+1,p,l);
        pardir[l+1]='\0';
    } else {
        pardir[0]='/'; pardir[1]='\0';
    }
    if(slash) {
        // strip trailing?
        int32_t st=ext2_dir_resolve(pardir,&par_ino);
        if(st<0) return st;
    } else par_ino=2;
    uint32_t exist;
    if(ext2_dir_find(par_ino, base, &exist, 0)==0) return -5;
    uint32_t nino=ext2_alloc_inode();
    if(!nino) return -4;
    uint8_t ib[256]={0};
    ext2_write_u16(ib, 0x81A4);
    ext2_write_u32(ib+4, 0);
    ext2_write_u16(ib+24,1);
    if(!ext2_write_inode(nino, ib)) return -1;
    int32_t r=ext2_dir_add_entry(par_ino, base, nino, 1);
    return r<0 ? r : 0;
}

int32_t ext2_file_close(int32_t d) {
    int idx = d - EXT2_DESCRIPTOR_BASE;
    if (idx < 0 || idx >= EXT2_MAX_OPEN || !g_handles[idx].used) {
        return -3;
    }
    g_handles[idx].used = false;
    return 0;
}

int32_t ext2_file_create(const char *path) {
    return ext2_create_file_internal(path);
}

int32_t ext2_file_write(const char *path, const void *buffer, uint32_t count) {
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st == -2) {
        st = ext2_create_file_internal(path);
        if (st < 0) return st;
        st = ext2_dir_resolve(path, &ino);
    }
    if (st < 0) return st;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    uint16_t mode = ext2_read_u16(ib);
    if ((mode & 0xF000) == 0x4000) return -6;
    if (count == 0) return ext2_write_data(ino, (const uint8_t*)buffer, 0);
    return ext2_write_data(ino, (const uint8_t*)buffer, count);
}

int32_t ext2_file_append(const char *path, const void *buffer, uint32_t count) {
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0) return ext2_file_write(path, buffer, count);
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    uint32_t old = ext2_read_u32(ib + 4);
    uint8_t *old_data = 0;
    if (old) {
        old_data = (uint8_t*)ext2_scratch_sector() + 2048;
        if (ext2_inode_read_data(ib, 0, old_data, old) < 0) return -1;
    }
    uint32_t nsize = old + count;
    uint8_t *combined = (uint8_t*)ext2_scratch_sector() + 4096;
    if (old) memcpy(combined, old_data, old);
    memcpy(combined + old, buffer, count);
    return ext2_write_data(ino, combined, nsize);
}

int32_t ext2_file_create_dir(const char *path) {
    const char *p = path;
    while (*p == '/') p++;
    if (!*p) return -3;
    const char *slash = 0;
    for (const char *t = p; *t; t++) if (*t == '/') slash = t;
    char pardir[256], base[64] = {0};
    if (slash) {
        uint32_t l = slash - p;
        pardir[0] = '/'; memcpy(pardir + 1, p, l); pardir[l + 1] = '\0';
        const char *b = slash + 1;
        uint32_t bl = strlen(b);
        if (bl >= sizeof(base)) return -3;
        memcpy(base, b, bl);
    } else {
        pardir[0] = '/'; pardir[1] = '\0';
        uint32_t bl = strlen(p);
        if (bl >= sizeof(base)) return -3;
        memcpy(base, p, bl);
    }
    uint32_t par;
    int32_t st = ext2_dir_resolve(pardir, &par);
    if (st < 0) return st;
    uint32_t exist;
    if (ext2_dir_find(par, base, &exist, 0) == 0) return -5;
    return ext2_dir_create(par, base);
}
