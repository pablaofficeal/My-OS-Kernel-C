#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/storage/storage_types.h"
#include "../../kernel/syscall.h"

struct pc_display_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size_bytes;
    uint8_t bpp;
    bool available;
};

int64_t pc_syscall(uint64_t number, uint64_t argument1,
                   uint64_t argument2, uint64_t argument3);
uint32_t pc_strlen(const char *text);
int pc_strcmp(const char *left, const char *right);
void pc_copy(char *destination, const char *source, uint32_t capacity);
void pc_write(const char *text);
void pc_write_u64(uint64_t value);
void pc_write_i64(int64_t value);
void pc_read_line(const char *prompt, char *buffer, uint32_t capacity);
void pc_sleep(uint32_t milliseconds);
int32_t pc_getpid(void);
int32_t pc_exec(const char *path);
int32_t pc_wait(int32_t pid, int32_t *status, bool nohang);
int32_t pc_try_getchar(void);
bool pc_file_exists(const char *path);
bool pc_display_get_info(struct pc_display_info *info);
void pc_display_begin_update(void);
void pc_display_end_update(void);
void pc_display_clear(uint32_t color);
void pc_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color);
void pc_draw_text(uint32_t x, uint32_t y, const char *text,
                  uint32_t foreground, uint32_t background);
bool pc_mouse_get(struct mouse_state *state);
int32_t pc_list_disks(struct storage_device_info *devices, uint32_t capacity);
int32_t pc_install_start(const char *device, const char *serial);
bool pc_install_status(struct install_status *status);
void pc_exit(int32_t status) __attribute__((noreturn));
