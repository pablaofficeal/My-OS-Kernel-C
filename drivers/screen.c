// drivers/screen.c
#include "screen.h"

volatile unsigned short *video_memory = (volatile unsigned short*)VIDEO_MEMORY;

int cursor_x = 0;
int cursor_y = 0;
unsigned char text_color = 0x07;

void clear_screen() {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_memory[i] = (text_color << 8) | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
}

void scroll() {
    if (cursor_y >= SCREEN_HEIGHT) {
        for (int y = 0; y < SCREEN_HEIGHT - 1; y++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                video_memory[y * SCREEN_WIDTH + x] = video_memory[(y + 1) * SCREEN_WIDTH + x];
            }
        }
        
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            video_memory[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH + x] = (text_color << 8) | ' ';
        }
        cursor_y = SCREEN_HEIGHT - 1;
    }
}

void putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~3;
    } else {
        video_memory[cursor_y * SCREEN_WIDTH + cursor_x] = (text_color << 8) | c;
        cursor_x++;
    }
    
    if (cursor_x >= SCREEN_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    scroll();
}

void print(const char *str) {
    while (*str) {
        putchar(*str++);
    }
}

// Simple printf (will be improved later)
void printf(const char *format, ...) {
    print(format);
}

void set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

int get_cursor_x() { return cursor_x; }
int get_cursor_y() { return cursor_y; }