#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "../../lib/string.h"

bool ext2_group_desc_read(uint32_t group, uint8_t *out) {
    struct ext2_volume *vol = ext2_volume();
    uint32_t gd_block = (vol->block_size == 1024) ? 2 : 1;
    uint32_t per_block = vol->block_size / 32;
    uint32_t block = gd_block + group / per_block;
    uint32_t index = group % per_block;
    uint8_t *tmp = ext2_scratch_block();
    if (!ext2_read_block(block, tmp)) {
        return false;
    }
    memcpy(out, tmp + index * 32, 32);
    return true;
}

bool ext2_inode_read(uint32_t ino, uint8_t *out) {
    struct ext2_volume *vol = ext2_volume();
    if (ino == 0 || ino > vol->total_inodes) {
        return false;
    }
    uint32_t group = (ino - 1) / vol->inodes_per_group;
    uint32_t index = (ino - 1) % vol->inodes_per_group;
    uint8_t gd[32];
    if (!ext2_group_desc_read(group, gd)) {
        return false;
    }
    uint32_t table = ext2_read_u32(gd + 8);
    uint32_t blk_off = index / vol->inodes_per_block;
    uint32_t off = (index % vol->inodes_per_block) * vol->inode_size;
    uint32_t block = table + blk_off;
    uint8_t *tmp = ext2_scratch_block();
    if (!ext2_read_block(block, tmp)) {
        return false;
    }
    memcpy(out, tmp + off, vol->inode_size);
    return true;
}

uint32_t ext2_inode_block_ptr(const uint8_t *inode, uint32_t logical) {
    struct ext2_volume *vol = ext2_volume();
    const uint8_t *p = inode + 40;
    if (logical < 12) {
        return ext2_read_u32(p + logical * 4);
    }
    uint32_t per = vol->block_size / 4;
    if (logical < 12 + per) {
        uint32_t ind = ext2_read_u32(p + 12 * 4);
        if (ind == 0) {
            return 0;
        }
        uint8_t *tmp = ext2_scratch_block();
        if (!ext2_read_block(ind, tmp)) {
            return 0;
        }
        return ext2_read_u32(tmp + (logical - 12) * 4);
    }
    if (logical < 12 + per + per * per) {
        uint32_t dind = ext2_read_u32(p + 13 * 4);
        if (dind == 0) {
            return 0;
        }
        uint32_t rem = logical - (12 + per);
        uint32_t i1 = rem / per;
        uint32_t i2 = rem % per;
        uint8_t *tmp = ext2_scratch_block();
        if (!ext2_read_block(dind, tmp)) {
            return 0;
        }
        uint32_t ind = ext2_read_u32(tmp + i1 * 4);
        if (ind == 0) {
            return 0;
        }
        uint8_t buf2[4096];
        if (!ext2_read_block(ind, buf2)) {
            return 0;
        }
        return ext2_read_u32(buf2 + i2 * 4);
    }
    return 0;
}

int32_t ext2_inode_read_data(const uint8_t *inode, uint32_t offset, void *buf, uint32_t count) {
    struct ext2_volume *vol = ext2_volume();
    uint32_t size = ext2_read_u32(inode + 4);
    if (offset >= size) {
        return 0;
    }
    if (offset + count > size) {
        count = size - offset;
    }
    uint8_t *out = (uint8_t *)buf;
    uint32_t total = 0;
    while (total < count) {
        uint32_t cur = offset + total;
        uint32_t logical = cur / vol->block_size;
        uint32_t off = cur % vol->block_size;
        uint32_t block = ext2_inode_block_ptr(inode, logical);
        if (block == 0) {
            uint32_t chunk = vol->block_size - off;
            if (chunk > count - total) {
                chunk = count - total;
            }
            memset(out + total, 0, chunk);
            total += chunk;
            continue;
        }
        uint8_t *tmp = ext2_scratch_block();
        if (!ext2_read_block(block, tmp)) {
            return -1;
        }
        uint32_t chunk = vol->block_size - off;
        if (chunk > count - total) {
            chunk = count - total;
        }
        memcpy(out + total, tmp + off, chunk);
        total += chunk;
    }
    return (int32_t)total;
}
