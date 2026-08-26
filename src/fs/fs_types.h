#pragma once

#include <stdint.h>

#define FS_DIRECTORY_NAME_CAPACITY 13

struct fs_directory_entry {
    char name[FS_DIRECTORY_NAME_CAPACITY];
    uint32_t size;
    uint8_t attributes;
};
