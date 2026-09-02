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

int32_t ext2_file_close(int32_t d) {
    int idx = d - EXT2_DESCRIPTOR_BASE;
    if (idx < 0 || idx >= EXT2_MAX_OPEN || !g_handles[idx].used) {
        return -3;
    }
    g_handles[idx].used = false;
    return 0;
}
