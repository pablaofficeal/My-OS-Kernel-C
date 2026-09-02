#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PF_API_VERSION 1
#define PF_VERSION "V1.0.0"
#define PF_VERSION_MAJOR 1
#define PF_VERSION_MINOR 0
#define PF_VERSION_PATCH 0

#define PF_NAME_CAPACITY 13
#define PF_PATH_CAPACITY 128

#define PF_ERROR_IO          -1
#define PF_ERROR_NOT_FOUND   -2
#define PF_ERROR_INVALID     -3
#define PF_ERROR_NO_SPACE    -4
#define PF_ERROR_EXISTS      -5
#define PF_ERROR_NOT_FILE    -6
#define PF_ERROR_NOT_DIR     -7
#define PF_ERROR_UNSUPPORTED -8
#define PF_ERROR_BUSY        -9
#define PF_ERROR_READ_ONLY  -10

#define PF_ATTR_DIRECTORY 0x10

struct pf_entry {
    char name[PF_NAME_CAPACITY];
    uint32_t size;
    uint8_t attributes;
};

const char *pf_version(void);
const char *pf_strerror(int32_t error);
bool pf_is_dir(const struct pf_entry *entry);

// low-level file ops (аналог pc_file_* но без зависимости от fs_types/syscall)
bool pf_exists(const char *path);
int32_t pf_open(const char *path);
int32_t pf_read(int32_t fd, void *buffer, uint32_t capacity);
int32_t pf_close(int32_t fd);
int32_t pf_write_file(const char *path, const void *buffer, uint32_t size);
int32_t pf_create_file(const char *path);
int32_t pf_delete(const char *path);
int32_t pf_rename(const char *path, const char *new_name);
int32_t pf_move(const char *path, const char *destination_directory);

// directory ops
int32_t pf_list(const char *path, struct pf_entry *entries, uint32_t capacity);
int32_t pf_create_dir(const char *path);

// convenience: open+read+close за один вызов, возвращает кол-во байт или <0
int32_t pf_read_all(const char *path, void *buffer, uint32_t capacity);
