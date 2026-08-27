#pragma once

#include <stdbool.h>
#include <stdint.h>

void monitor_run(void);
void monitor_window_draw(void);
void monitor_window_close(void);
bool monitor_window_is_visible(void);
bool monitor_window_contains_point(int32_t x, int32_t y);
bool monitor_window_handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                                 bool pressed, bool released,
                                 uint32_t screen_width,
                                 uint32_t screen_height);
void monitor_window_update(void);
