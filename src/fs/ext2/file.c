#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "include/ext2_dir.h"
#include "include/ext2_file.h"
#include "../../lib/string.h"
#include "../../kernel/diagnostics/klog.h"

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

static int32_t ext2_write_data(uint32_t ino, const uint8_t *data, uint32_t size) {
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    uint32_t bcnt = (size + 1023) / 1024;
    memset(ib + 40, 0, 60);
    uint8_t single[1024] = {0};
    uint8_t dind_buf[1024] = {0};
    uint8_t tind_buf[1024] = {0};
    uint32_t single_blk = 0, dind_blk = 0, tind_blk = 0;
    uint32_t single_used = 0, dind_used = 0, tind_used = 0;
    uint8_t cur_ind[1024] = {0};
    uint32_t cur_ind_blk = 0;
    for (uint32_t i = 0; i < bcnt; i++) {
        uint32_t nb = ext2_alloc_block();
        if (!nb) { klogf(KLOG_ERROR,"ext2: alloc block failed i=%u bcnt=%u size=%u",i,bcnt,size); return -4; }
        uint8_t tmp[1024] = {0};
        uint32_t off = i * 1024;
        uint32_t left = size - off;
        uint32_t cur = left > 1024 ? 1024 : left;
        memcpy(tmp, data + off, cur);
        if (!ext2_write_block(nb, tmp)) return -1;
        if (i < 12) {
            ext2_write_u32(ib + 40 + i * 4, nb);
        } else if (i < 12 + 256) {
            if (!single_blk) { single_blk = ext2_alloc_block(); if (!single_blk) return -4; }
            ext2_write_u32(single + (i - 12) * 4, nb);
            single_used = i - 12 + 1;
        } else if (i < 12 + 256 + 256 * 256) {
            if (!dind_blk) { dind_blk = ext2_alloc_block(); if (!dind_blk) return -4; }
            uint32_t idx = i - (12 + 256);
            uint32_t which = idx / 256;
            uint32_t inner = idx % 256;
            if (which != dind_used) {
                if (cur_ind_blk) {
                    if (!ext2_write_block(cur_ind_blk, cur_ind)) return -1;
                    ext2_write_u32(dind_buf + (which - 1) * 4, cur_ind_blk);
                }
                cur_ind_blk = ext2_alloc_block();
                if (!cur_ind_blk) return -4;
                memset(cur_ind, 0, 1024);
                dind_used = which + 1;
            } else if (!cur_ind_blk) {
                cur_ind_blk = ext2_alloc_block();
                if (!cur_ind_blk) return -4;
                memset(cur_ind, 0, 1024);
                dind_used = 1;
            }
            ext2_write_u32(cur_ind + inner * 4, nb);
            if (inner == 255 || i + 1 == bcnt) {
                if (!ext2_write_block(cur_ind_blk, cur_ind)) return -1;
                ext2_write_u32(dind_buf + which * 4, cur_ind_blk);
                cur_ind_blk = 0;
            }
        } else {
            if (!tind_blk) { tind_blk = ext2_alloc_block(); if (!tind_blk) return -4; memset(tind_buf,0,1024); }
            uint32_t idx = i - (12 + 256 + 65536);
            uint32_t d = idx / (256 * 256);
            uint32_t r = idx % (256 * 256);
            uint32_t w = r / 256;
            uint32_t inner = r % 256;
            if (d >= 256) return -4;
            uint32_t dblk = ext2_read_u32(tind_buf + d * 4);
            if (!dblk) {
                dblk = ext2_alloc_block();
                if (!dblk) return -4;
                uint8_t zero[1024]={0};
                if (!ext2_write_block(dblk, zero)) return -1;
                ext2_write_u32(tind_buf + d * 4, dblk);
            }
            uint8_t dbuf2[1024];
            if (!ext2_read_block(dblk, dbuf2)) memset(dbuf2,0,1024);
            uint32_t iblk = ext2_read_u32(dbuf2 + w * 4);
            if (!iblk) {
                iblk = ext2_alloc_block();
                if (!iblk) return -4;
                uint8_t zero2[1024]={0};
                if (!ext2_write_block(iblk, zero2)) return -1;
                ext2_write_u32(dbuf2 + w * 4, iblk);
                if (!ext2_write_block(dblk, dbuf2)) return -1;
            }
            uint8_t sbuf[1024];
            if (!ext2_read_block(iblk, sbuf)) memset(sbuf,0,1024);
            ext2_write_u32(sbuf + inner * 4, nb);
            if (!ext2_write_block(iblk, sbuf)) return -1;
        }
    }
    if (single_blk) {
        if (!ext2_write_block(single_blk, single)) return -1;
        ext2_write_u32(ib + 40 + 12 * 4, single_blk);
    }
    if (dind_blk) {
        if (cur_ind_blk) {
            if (!ext2_write_block(cur_ind_blk, cur_ind)) return -1;
            ext2_write_u32(dind_buf + (dind_used - 1) * 4, cur_ind_blk);
        }
        if (!ext2_write_block(dind_blk, dind_buf)) return -1;
        ext2_write_u32(ib + 40 + 13 * 4, dind_blk);
    }
    if (tind_blk) {
        if (!ext2_write_block(tind_blk, tind_buf)) return -1;
        ext2_write_u32(ib + 40 + 14 * 4, tind_blk);
    }
    ext2_write_u32(ib + 4, size);
    ext2_write_u32(ib + 28, bcnt * 2);
    ext2_write_u16(ib + 24, 1);
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
