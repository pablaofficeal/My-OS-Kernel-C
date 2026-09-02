#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "../../drivers/storage/block_device.h"

static struct ext2_volume g_vol;
static uint8_t g_sector[BLOCK_SECTOR_SIZE] __attribute__((aligned(2)));
static uint8_t g_block[4096] __attribute__((aligned(2)));

struct ext2_volume *ext2_volume(void) {
    return &g_vol;
}

uint8_t *ext2_scratch_sector(void) {
    return g_sector;
}

uint8_t *ext2_scratch_block(void) {
    return g_block;
}

uint16_t ext2_read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t ext2_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ext2_write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void ext2_write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool ext2_read_block(uint32_t block, void *out) {
    if (!g_vol.mounted) {
        return false;
    }
    uint32_t spb = g_vol.block_size / BLOCK_SECTOR_SIZE;
    uint32_t lba = g_vol.partition_lba + block * spb;
    uint8_t *dst = (uint8_t *)out;
    for (uint32_t i = 0; i < spb; i++) {
        if (!block_device_read(lba + i, dst + i * BLOCK_SECTOR_SIZE)) {
            return false;
        }
    }
    return true;
}

bool ext2_write_block(uint32_t block, const void *in) {
    uint32_t spb = g_vol.block_size / BLOCK_SECTOR_SIZE;
    uint32_t lba = g_vol.partition_lba + block * spb;
    const uint8_t *src = (const uint8_t *)in;
    for (uint32_t i = 0; i < spb; i++) {
        if (!block_device_write(lba + i, src + i * BLOCK_SECTOR_SIZE)) {
            return false;
        }
    }
    return true;
}

bool ext2_group_write(uint32_t group, const uint8_t *gd) {
    uint32_t gd_block = (g_vol.block_size == 1024) ? 2 : 1;
    uint32_t per = g_vol.block_size / 32;
    uint32_t block = gd_block + group / per;
    uint32_t idx = group % per;
    if (!ext2_read_block(block, g_block)) return false;
    for (int i = 0; i < 32; i++) g_block[idx * 32 + i] = gd[i];
    return ext2_write_block(block, g_block);
}

bool ext2_write_inode(uint32_t ino, const uint8_t *in) {
    if (ino == 0 || ino > g_vol.total_inodes) return false;
    uint32_t group = (ino - 1) / g_vol.inodes_per_group;
    uint32_t index = (ino - 1) % g_vol.inodes_per_group;
    uint8_t gd[32];
    uint32_t gd_block = (g_vol.block_size == 1024) ? 2 : 1;
    uint32_t per = g_vol.block_size / 32;
    uint32_t b = gd_block + group / per;
    uint32_t idx = group % per;
    if (!ext2_read_block(b, g_block)) return false;
    for (int i = 0; i < 32; i++) gd[i] = g_block[idx * 32 + i];
    uint32_t table = ext2_read_u32(gd + 8);
    uint32_t blk_off = index / g_vol.inodes_per_block;
    uint32_t off = (index % g_vol.inodes_per_block) * g_vol.inode_size;
    uint32_t block = table + blk_off;
    if (!ext2_read_block(block, g_block)) return false;
    for (uint32_t i = 0; i < g_vol.inode_size; i++) g_block[off + i] = in[i];
    return ext2_write_block(block, g_block);
}

uint32_t ext2_alloc_block(void) {
    uint32_t gd_block = (g_vol.block_size == 1024) ? 2 : 1;
    uint32_t per = g_vol.block_size / 32;
    if (!ext2_read_block(3, g_block)) return 0;
    uint32_t total = g_vol.total_blocks;
    for (uint32_t b = 135; b < total; b++) {
        uint32_t byte = b / 8;
        uint32_t bit = b % 8;
        if (byte >= g_vol.block_size) break;
        if ((g_block[byte] & (1u << bit)) == 0) {
            g_block[byte] |= (1u << bit);
            if (!ext2_write_block(3, g_block)) return 0;
            uint8_t gd[32];
            if (!ext2_read_block(gd_block, g_block)) return 0;
            for (int i = 0; i < 32; i++) gd[i] = g_block[i];
            uint32_t free = ext2_read_u16(gd + 12);
            if (free) ext2_write_u16(gd + 12, free - 1);
            g_block[0] = gd[0]; g_block[1]=gd[1]; g_block[2]=gd[2]; g_block[3]=gd[3];
            ext2_group_write(0, gd);
            uint8_t zero[4096] = {0};
            ext2_write_block(b, zero);
            return b;
        }
    }
    return 0;
}

uint32_t ext2_alloc_inode(void) {
    if (!ext2_read_block(4, g_block)) return 0;
    for (uint32_t i = 12; i < g_vol.inodes_per_group; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if ((g_block[byte] & (1u << bit)) == 0) {
            g_block[byte] |= (1u << bit);
            if (!ext2_write_block(4, g_block)) return 0;
            uint8_t gd[32];
            uint32_t gd_block = (g_vol.block_size == 1024) ? 2 : 1;
            if (!ext2_read_block(gd_block, g_block)) return 0;
            for (int j = 0; j < 32; j++) gd[j] = g_block[j];
            uint32_t free = ext2_read_u16(gd + 14);
            if (free) ext2_write_u16(gd + 14, free - 1);
            ext2_group_write(0, gd);
            return i + 1;
        }
    }
    return 0;
}
