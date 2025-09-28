// drivers/screen.h
#ifndef SCREEN_H
#define SCREEN_H

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define VIDEO_MEMORY 0xB8000

void clear_screen();
void putchar(char c);
void print(const char *str);
void printf(const char *format, ...);
void set_cursor(int x, int y);
int get_cursor_x();
int get_cursor_y();

#endif