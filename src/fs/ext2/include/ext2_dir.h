#pragma once

#include <stdint.h>
#include "../../types/fs_types.h"

int32_t ext2_dir_find(uint32_t dir_ino, const char *name, uint32_t *out_ino, uint8_t *out_type);
int32_t ext2_dir_resolve(const char *path, uint32_t *out_ino);
int32_t ext2_dir_list(uint32_t dir_ino, struct fs_directory_entry *entries, uint32_t capacity);
