#pragma once

#include <stdint.h>

int32_t ext2_file_open(const char *path);
int32_t ext2_file_read(int32_t descriptor, void *buffer, uint32_t count);
int32_t ext2_file_close(int32_t descriptor);
void ext2_file_handles_reset(void);
