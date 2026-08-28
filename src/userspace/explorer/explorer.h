#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void explorer_open(uint32_t screen_width, uint32_t screen_height);
void explorer_window_draw(void);
void explorer_window_close(void);
bool explorer_window_is_visible(void);
bool explorer_window_contains_point(int32_t x, int32_t y);
bool explorer_window_handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                                  bool pressed, bool released,
                                  uint32_t screen_width,
                                  uint32_t screen_height);
bool explorer_window_handle_key(char key);

#ifdef __cplusplus
}
#endif
