#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FS_ERROR_IO          -1
#define FS_ERROR_NOT_FOUND   -2
#define FS_ERROR_INVALID     -3
#define FS_ERROR_NO_SPACE    -4
#define FS_ERROR_EXISTS      -5
#define FS_ERROR_NOT_FILE    -6
#define FS_ERROR_NOT_DIR     -7
#define FS_ERROR_UNSUPPORTED -8
#define FS_ERROR_BUSY        -9
#define FS_ERROR_READ_ONLY  -10

bool fat32_init(void);
bool fat32_is_mounted(void);
const char *fat32_device_name(void);
int32_t fat32_open(const char *path);
int32_t fat32_read(int32_t descriptor, void *buffer, uint32_t count);
int32_t fat32_delete(const char *path);
int32_t fat32_rename(const char *path, const char *new_name);
int32_t fat32_move(const char *path, const char *destination_directory);
