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
