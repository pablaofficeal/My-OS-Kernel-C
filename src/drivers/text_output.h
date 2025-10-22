#ifndef TEXT_OUTPUT_H
#define TEXT_OUTPUT_H

#include <stdarg.h>
#include <stdint.h>
#include "../lib/stddef.h"

// Text output functions
void set_cursor(int x, int y);
void clear_screen(void);
int putchar(int c);
int printf(const char *format, ...);
void draw_char(int x, int y, char c, uint32_t color);

#endif