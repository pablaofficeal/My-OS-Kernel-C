#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_super.h"
#include "include/ext2_format.h"
#include "../../drivers/storage/block_device.h"
#include "../../lib/string.h"

int32_t ext2_format_at(uint32_t part_lba, uint32_t part_sectors);

int32_t ext2_format_device_impl(const char *device_name, const char *serial_confirmation, const char *erase_confirmation) {
    if (!device_name || !serial_confirmation || !erase_confirmation) {
        return -3;
    }
    int32_t idx = block_device_find(device_name);
    if (idx < 0) {
        return -2;
    }
    struct storage_device_info info;
    if (!block_device_get_info((uint32_t)idx, &info)) {
        return -3;
    }
    if (!info.operational || !info.writable) {
        return -10;
    }
    if (!info.serial[0] || strcmp(info.serial, serial_confirmation) != 0 || strcmp(erase_confirmation, "ERASE") != 0) {
        return -11;
    }
    if (info.sector_size != BLOCK_SECTOR_SIZE) {
        return -8;
    }
    if (!block_device_select((uint32_t)idx)) {
        return -3;
    }
    uint32_t total_blocks = (uint32_t)info.sector_count / (1024 / BLOCK_SECTOR_SIZE);
    if (total_blocks < 1024) {
        return -13;
    }
    return ext2_format_at(0, info.sector_count);
}

int32_t ext2_format_at(uint32_t part_lba, uint32_t part_sectors) {
    if (part_sectors > 0xFFFFFF00) part_sectors = 0xFFFFFF00;
    uint32_t total_blocks = part_sectors / (1024 / BLOCK_SECTOR_SIZE);
    if (total_blocks < 128) return -13;
    if (total_blocks > 0x7FFFFFFF) total_blocks = 0x7FFFFFFF;
    uint32_t max_groups = 32;
    uint32_t max_blocks = 8192 * max_groups;
    if (total_blocks > max_blocks) total_blocks = max_blocks;
    uint32_t blocks_per_group = 8192;
    uint32_t inodes_per_group = 1024;
    uint32_t groups = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    uint8_t *blk = ext2_scratch_block();
    uint8_t *sec = ext2_scratch_sector();
    uint32_t itb_blocks_tmp = 128;
    uint32_t meta_blocks_tmp = 2 + itb_blocks_tmp;
    uint32_t free_blocks_tmp = 0;
    for (uint32_t g = 0; g < groups; g++) {
        uint32_t grp_blocks = blocks_per_group;
        if (g == groups - 1) {
            uint32_t rem = total_blocks % blocks_per_group;
            if (rem) grp_blocks = rem;
        }
        uint32_t meta = (g == 0) ? 135 : meta_blocks_tmp;
        if (grp_blocks > meta) free_blocks_tmp += grp_blocks - meta;
    }
    uint32_t free_inodes_tmp = inodes_per_group * groups - 11;
    uint8_t sb[1024];
    memset(sb, 0, 1024);
    ext2_write_u32(sb + 0, inodes_per_group * groups);
    ext2_write_u32(sb + 4, total_blocks);
    ext2_write_u32(sb + 12, free_blocks_tmp);
    ext2_write_u32(sb + 16, free_inodes_tmp);
    ext2_write_u32(sb + 20, 1);
    ext2_write_u32(sb + 24, 0);
    ext2_write_u32(sb + 32, blocks_per_group);
    ext2_write_u32(sb + 36, 8192);
    ext2_write_u32(sb + 40, inodes_per_group);
    sb[56] = 0x53;
    sb[57] = 0xEF;
    ext2_write_u16(sb + 58, 1);
    ext2_write_u16(sb + 88, 128);
    ext2_write_u32(sb + 92, 1);
    uint32_t sb_lba = part_lba + 2;
    memcpy(sec, sb, 512);
    if (!block_device_write(sb_lba, sec)) {
        return -1;
    }
    memcpy(sec, sb + 512, 512);
    if (!block_device_write(sb_lba + 1, sec)) {
        return -1;
    }
    uint8_t gd[1024];
    memset(gd, 0, 1024);
    uint32_t itb_blocks = 128; // 1024 inodes *128 byte / 1024 block =128
    uint32_t meta_blocks = 2 + itb_blocks; // bmb+imb+itb
    for (uint32_t g = 0; g < groups && g < 32; g++) {
        uint32_t bmb, imb, itb;
        uint16_t free_blocks, free_inodes;
        if (g == 0) { bmb = 3; imb = 4; itb = 5; free_blocks = 8192 - 135; free_inodes = 1024 - 11; }
        else { bmb = g * 8192; imb = bmb + 1; itb = bmb + 2; free_blocks = 8192 - meta_blocks; free_inodes = 1024; }
        ext2_write_u32(gd + g * 32, bmb);
        ext2_write_u32(gd + g * 32 + 4, imb);
        ext2_write_u32(gd + g * 32 + 8, itb);
        ext2_write_u16(gd + g * 32 + 12, free_blocks);
        ext2_write_u16(gd + g * 32 + 14, free_inodes);
        if (g == 0) ext2_write_u16(gd + g * 32 + 16, 2);
    }
    memset(blk, 0, 1024);
    memcpy(blk, gd, 1024);
    uint32_t gd_lba = part_lba + 4;
    memcpy(sec, blk, 512);
    if (!block_device_write(gd_lba, sec)) {
        return -1;
    }
    memcpy(sec, blk + 512, 512);
    if (!block_device_write(gd_lba + 1, sec)) {
        return -1;
    }
    memset(blk, 0, 1024);
    for (uint32_t b = 0; b < 135; b++) {
        blk[b / 8] |= (uint8_t)(1 << (b % 8));
    }
    uint32_t bm_lba = part_lba + 6;
    memcpy(sec, blk, 512);
    if (!block_device_write(bm_lba, sec)) {
        return -1;
    }
    memcpy(sec, blk + 512, 512);
    if (!block_device_write(bm_lba + 1, sec)) {
        return -1;
    }
    memset(blk, 0, 1024);
    for (int i = 0; i < 11; i++) {
        blk[i / 8] |= (uint8_t)(1 << (i % 8));
    }
    uint32_t ibm_lba = part_lba + 8;
    memcpy(sec, blk, 512);
    if (!block_device_write(ibm_lba, sec)) {
        return -1;
    }
    memcpy(sec, blk + 512, 512);
    if (!block_device_write(ibm_lba + 1, sec)) {
        return -1;
    }
    for (uint32_t b = 0; b < 128; b++) {
        memset(blk, 0, 1024);
        if (b == 0) {
            uint8_t *ino2 = blk + 128;
            ext2_write_u16(ino2 + 0, 0x41ED);
            ext2_write_u32(ino2 + 4, 1024);
            ext2_write_u32(ino2 + 28, 1);
            ext2_write_u32(ino2 + 40, 134);
            ext2_write_u16(ino2 + 24, 1);
        }
        uint32_t lba = part_lba + 10 + b * 2;
        memcpy(sec, blk, 512);
        if (!block_device_write(lba, sec)) {
            return -1;
        }
        memcpy(sec, blk + 512, 512);
        if (!block_device_write(lba + 1, sec)) {
            return -1;
        }
    }
    memset(blk, 0, 1024);
    blk[0] = 2;
    ext2_write_u16(blk + 4, 12);
    blk[6] = 1;
    blk[7] = 2;
    blk[8] = '.';
    blk[12] = 2;
    ext2_write_u16(blk + 16, 1012);
    blk[18] = 2;
    blk[19] = 2;
    blk[20] = '.';
    blk[21] = '.';
    uint32_t root_lba = part_lba + 134 * 2;
    memcpy(sec, blk, 512);
    if (!block_device_write(root_lba, sec)) {
        return -1;
    }
    memcpy(sec, blk + 512, 512);
    if (!block_device_write(root_lba + 1, sec)) {
        return -1;
    }
    for (uint32_t g = 1; g < groups && g < 32; g++) {
        uint32_t bmb = part_lba + g * 8192 * 2;
        // Reserve first meta_blocks bits in block bitmap (bmb, imb, inode tables)
        memset(blk, 0, 1024);
        for (uint32_t b = 0; b < meta_blocks; b++) blk[b / 8] |= (uint8_t)(1 << (b % 8));
        memcpy(sec, blk, 512);
        if (!block_device_write(bmb, sec)) return -1;
        memcpy(sec, blk + 512, 512);
        if (!block_device_write(bmb + 1, sec)) return -1;
        uint32_t imb = bmb + 2;
        memset(blk, 0, 1024);
        memcpy(sec, blk, 512);
        if (!block_device_write(imb, sec)) return -1;
        memcpy(sec, blk + 512, 512);
        if (!block_device_write(imb + 1, sec)) return -1;
        uint32_t itb = imb + 2;
        for (uint32_t b = 0; b < itb_blocks; b++) {
            memset(blk, 0, 1024);
            memcpy(sec, blk, 512);
            if (!block_device_write(itb + b * 2, sec)) return -1;
            memcpy(sec, blk + 512, 512);
            if (!block_device_write(itb + b * 2 + 1, sec)) return -1;
        }
    }
    if (!block_device_flush()) {
        return -1;
    }
    memset(ext2_volume(), 0, sizeof(struct ext2_volume));
    return 0;
}
