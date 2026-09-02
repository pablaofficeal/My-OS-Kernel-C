#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "include/ext2_dir.h"
#include "../../lib/string.h"

int32_t ext2_dir_find(uint32_t dir_ino, const char *name, uint32_t *out_ino, uint8_t *out_type) {
    uint8_t ibuf[256];
    if (!ext2_inode_read(dir_ino, ibuf)) {
        return -1;
    }
    uint16_t mode = ext2_read_u16(ibuf);
    if ((mode & 0xF000) != EXT2_S_IFDIR) {
        return -7;
    }
    uint32_t size = ext2_read_u32(ibuf + 4);
    uint32_t offset = 0;
    size_t nlen = strlen(name);
    struct ext2_volume *vol = ext2_volume();
    while (offset < size) {
        uint32_t logical = offset / vol->block_size;
        uint32_t off = offset % vol->block_size;
        uint32_t block = ext2_inode_block_ptr(ibuf, logical);
        if (block == 0) {
            return -2;
        }
        uint8_t *blk = ext2_scratch_block();
        if (!ext2_read_block(block, blk)) {
            return -1;
        }
        if (off + 8 > vol->block_size) {
            offset = (logical + 1) * vol->block_size;
            continue;
        }
        uint8_t *e = blk + off;
        uint32_t ino = ext2_read_u32(e);
        uint16_t rec = ext2_read_u16(e + 4);
        uint8_t nl = e[6];
        if (rec == 0 || rec > vol->block_size - off) {
            return -3;
        }
        if (ino != 0 && nl == nlen && memcmp(e + 8, name, nlen) == 0) {
            if (out_ino) {
                *out_ino = ino;
            }
            if (out_type) {
                *out_type = e[7];
            }
            return 0;
        }
        offset += rec;
    }
    return -2;
}

int32_t ext2_dir_resolve(const char *path, uint32_t *out_ino) {
    struct ext2_volume *vol = ext2_volume();
    if (!vol->mounted || !path || !path[0]) {
        return -3;
    }
    const char *p = path;
    while (*p == '/') {
        p++;
    }
    if (!*p) {
        *out_ino = EXT2_ROOT_INO;
        return 0;
    }
    uint32_t cur = EXT2_ROOT_INO;
    char comp[EXT2_NAME_MAX + 1];
    while (*p) {
        uint32_t len = 0;
        while (*p && *p != '/') {
            if (len >= EXT2_NAME_MAX) {
                return -3;
            }
            comp[len++] = *p++;
        }
        comp[len] = '\0';
        while (*p == '/') {
            p++;
        }
        uint32_t next;
        int32_t st = ext2_dir_find(cur, comp, &next, 0);
        if (st < 0) {
            return st;
        }
        cur = next;
        if (!*p) {
            *out_ino = cur;
            return 0;
        }
        uint8_t ib[256];
        if (!ext2_inode_read(cur, ib)) {
            return -1;
        }
        uint16_t mode = ext2_read_u16(ib);
        if ((mode & 0xF000) != EXT2_S_IFDIR) {
            return -7;
        }
    }
    return -3;
}

int32_t ext2_dir_list(uint32_t dir_ino, struct fs_directory_entry *entries, uint32_t capacity) {
    uint8_t ibuf[256];
    if (!ext2_inode_read(dir_ino, ibuf)) {
        return -1;
    }
    uint16_t mode = ext2_read_u16(ibuf);
    if ((mode & 0xF000) != EXT2_S_IFDIR) {
        return -7;
    }
    uint32_t size = ext2_read_u32(ibuf + 4);
    uint32_t offset = 0;
    uint32_t count = 0;
    struct ext2_volume *vol = ext2_volume();
    while (offset < size && count < capacity) {
        uint32_t logical = offset / vol->block_size;
        uint32_t off = offset % vol->block_size;
        uint32_t block = ext2_inode_block_ptr(ibuf, logical);
        if (block == 0) {
            break;
        }
        uint8_t *blk = ext2_scratch_block();
        if (!ext2_read_block(block, blk)) {
            return -1;
        }
        if (off + 8 > vol->block_size) {
            offset = (logical + 1) * vol->block_size;
            continue;
        }
        uint8_t *e = blk + off;
        uint32_t ino = ext2_read_u32(e);
        uint16_t rec = ext2_read_u16(e + 4);
        uint8_t nl = e[6];
        if (rec == 0) {
            break;
        }
        if (ino != 0) {
            if (nl > 0 && nl < FS_DIRECTORY_NAME_CAPACITY) {
                bool dot = (nl == 1 && e[8] == '.') || (nl == 2 && e[8] == '.' && e[9] == '.');
                if (!dot) {
                    memcpy(entries[count].name, e + 8, nl);
                    entries[count].name[nl] = '\0';
                    uint8_t eib[256];
                    if (ext2_inode_read(ino, eib)) {
                        entries[count].size = ext2_read_u32(eib + 4);
                        uint16_t em = ext2_read_u16(eib);
                        entries[count].attributes = ((em & 0xF000) == EXT2_S_IFDIR) ? FS_ATTRIBUTE_DIRECTORY : 0;
                    } else {
                        entries[count].size = 0;
                        entries[count].attributes = 0;
                    }
                    count++;
                    if (count == capacity) {
                        return (int32_t)count;
                    }
                }
            }
        }
        offset += rec;
    }
    return (int32_t)count;
}

int32_t ext2_dir_add_entry(uint32_t dir_ino, const char *name, uint32_t ino, uint8_t file_type) {
    uint8_t dib[256];
    if (!ext2_inode_read(dir_ino, dib)) return -1;
    uint32_t size = ext2_read_u32(dib + 4);
    struct ext2_volume *vol = ext2_volume();
    uint32_t block = ext2_inode_block_ptr(dib, 0);
    if (block == 0) return -1;
    uint8_t *blk = ext2_scratch_block();
    if (!ext2_read_block(block, blk)) return -1;
    uint32_t nlen = strlen(name);
    uint32_t rec = 8 + ((nlen + 3) & ~3);
    uint32_t off = 0;
    while (off < vol->block_size) {
        uint8_t *e = blk + off;
        uint16_t cur = ext2_read_u16(e + 4);
        uint32_t e_ino = ext2_read_u32(e);
        if (e_ino == 0) {
            if (cur >= rec) {
                ext2_write_u32(e, ino);
                ext2_write_u16(e + 4, cur);
                e[6] = (uint8_t)nlen;
                e[7] = file_type;
                memcpy(e + 8, name, nlen);
                return ext2_write_block(block, blk) ? 0 : -1;
            }
            return -4;
        }
        uint8_t nl = e[6];
        uint32_t ideal = 8 + ((nl + 3) & ~3);
        if (off + cur >= size) {
            uint32_t remain = cur - ideal;
            if (remain < rec) return -4;
            ext2_write_u16(e + 4, (uint16_t)ideal);
            uint8_t *ne = e + ideal;
            ext2_write_u32(ne, ino);
            ext2_write_u16(ne + 4, (uint16_t)remain);
            ne[6] = (uint8_t)nlen;
            ne[7] = file_type;
            memcpy(ne + 8, name, nlen);
            uint32_t new_size = off + ideal + remain;
            if (new_size > size) {
                ext2_write_u32(dib + 4, new_size);
                ext2_write_inode(dir_ino, dib);
            }
            return ext2_write_block(block, blk) ? 0 : -1;
        }
        off += cur;
    }
    return -4;
}

int32_t ext2_dir_create(uint32_t parent_ino, const char *name) {
    uint32_t new_ino = ext2_alloc_inode();
    if (!new_ino) return -4;
    uint32_t blk = ext2_alloc_block();
    if (!blk) return -4;
    uint8_t ib[256] = {0};
    ext2_write_u16(ib, 0x41ED);
    ext2_write_u16(ib + 2, 0);
    ext2_write_u32(ib + 4, 1024);
    ext2_write_u32(ib + 28, 2);
    ext2_write_u32(ib + 40, blk);
    ext2_write_u16(ib + 24, 2);
    if (!ext2_write_inode(new_ino, ib)) return -1;
    uint8_t *b = ext2_scratch_block();
    memset(b, 0, 1024);
    b[0] = (uint8_t)new_ino; b[1] = 0; ext2_write_u16(b + 4, 12); b[6]=1; b[7]=2; b[8]='.';
    b[12]= (uint8_t)parent_ino; b[13]=0; ext2_write_u16(b+16, 1012); b[18]=2; b[19]=2; b[20]='.'; b[21]='.';
    if (!ext2_write_block(blk, b)) return -1;
    int32_t r = ext2_dir_add_entry(parent_ino, name, new_ino, 2);
    if (r < 0) return r;
    uint8_t pb[256];
    if (ext2_inode_read(parent_ino, pb)) {
        uint16_t links = ext2_read_u16(pb + 26);
        ext2_write_u16(pb + 26, links + 1);
        ext2_write_inode(parent_ino, pb);
    }
    return 0;
}
