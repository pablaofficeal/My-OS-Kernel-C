#pragma once
#include <stdbool.h>
#include <stdint.h>

void terminal_init(uint32_t screen_width, uint32_t screen_height);
void terminal_handle_key(char c);
void terminal_redraw(void);
void terminal_set_visible(bool visible);
bool terminal_is_visible(void);
bool terminal_contains_point(int32_t x, int32_t y);
bool terminal_handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                           bool pressed, bool released,
                           uint32_t screen_width, uint32_t screen_height);

void terminal_putc(char c);
void terminal_write(const char *text);
void terminal_write_colored(const char *text, uint32_t color);
void terminal_printf(const char *format, ...);
void terminal_clear(void);
void terminal_prompt(void);
bool terminal_set_font_size(uint32_t size);
uint32_t terminal_get_font_size(void);
bool terminal_set_font_face(const char *name);
const char *terminal_get_font_face(void);

uint32_t terminal_get_window_width(void);
uint32_t terminal_get_window_height(void);
