#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fs_types.h"

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
#define FS_ERROR_CONFIRMATION -11
#define FS_ERROR_NOT_BLANK    -12
#define FS_ERROR_TOO_SMALL    -13

bool fat32_init(void);
bool fat32_is_mounted(void);
const char *fat32_device_name(void);
int32_t fat32_open(const char *path);
int32_t fat32_read(int32_t descriptor, void *buffer, uint32_t count);
int32_t fat32_delete(const char *path);
int32_t fat32_rename(const char *path, const char *new_name);
int32_t fat32_move(const char *path, const char *destination_directory);
int32_t fat32_list(const char *path, struct fs_directory_entry *entries,
                   uint32_t capacity);
int32_t fat32_create_file(const char *path);
int32_t fat32_create_directory(const char *path);
int32_t fat32_format_device(const char *device_name,
                            const char *serial_confirmation,
                            const char *erase_confirmation);
