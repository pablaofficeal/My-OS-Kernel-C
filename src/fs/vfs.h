#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fs_types.h"

#define VFS_FD_STDIN  0
#define VFS_FD_STDOUT 1
#define VFS_FD_STDERR 2
#define VFS_FD_BASE   3

bool vfs_mount_root(void);
bool vfs_is_root_mounted(void);
const char *vfs_root_device_name(void);
int32_t vfs_open(const char *path);
int32_t vfs_read(int32_t descriptor, void *buffer, uint32_t count);
int32_t vfs_close(int32_t descriptor);
int32_t vfs_delete(const char *path);
int32_t vfs_rename(const char *path, const char *new_name);
int32_t vfs_move(const char *path, const char *destination_directory);
int32_t vfs_list(const char *path, struct fs_directory_entry *entries,
                 uint32_t capacity);
int32_t vfs_create_file(const char *path);
int32_t vfs_write_file(const char *path, const void *buffer, uint32_t count);
int32_t vfs_append_file(const char *path, const void *buffer, uint32_t count);
int32_t vfs_create_directory(const char *path);
int32_t vfs_format_device(const char *device_name,
                          const char *serial_confirmation,
                          const char *erase_confirmation);
int32_t vfs_format_device_force(const char *device_name,
                                const char *serial_confirmation);
int32_t vfs_format_uefi_device(const char *device_name,
                               const char *serial_confirmation);
