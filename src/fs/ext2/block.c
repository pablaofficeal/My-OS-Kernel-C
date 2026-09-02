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
