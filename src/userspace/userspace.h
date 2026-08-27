#pragma once
#include <stdint.h>
#include <stdbool.h>

void userspace_init(void);
void userspace_run(void);
void userspace_input_thread(void *arg);
void userspace_terminal_thread(void *arg);
uint32_t userspace_get_width(void);
uint32_t userspace_get_height(void);
void userspace_set_mouse_debug(bool enabled);
