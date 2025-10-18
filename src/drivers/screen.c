// drivers/screen.c - ОБНОВЛЕННЫЙ
#include "screen.h"
#include "graphics.h"

volatile unsigned short *text_video_memory = (volatile unsigned short*)VIDEO_MEMORY;

int cursor_x = 0;
int cursor_y = 0;
unsigned char text_color = 0x07;
int current_mode = VGA_TEXT_MODE;

void clear_screen() {
    if (current_mode == VGA_TEXT_MODE) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            text_video_memory[i] = (text_color << 8) | ' ';
        }
        cursor_x = 0;
        cursor_y = 0;
    } else {
        clear_screen(COLOR_BLACK);
        swap_buffers();
    }
}

void putchar(char c) {
    if (current_mode == VGA_GRAPHICS_MODE) {
        // В графическом режиме используем графические функции
        // (нужно будет доработать для поддержки текстового вывода)
        return;
    }
    
    // Оригинальный код для текстового режима
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~3;
    } else {
        text_video_memory[cursor_y * SCREEN_WIDTH + cursor_x] = (text_color << 8) | c;
        cursor_x++;
    }
    
    if (cursor_x >= SCREEN_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    
    if (cursor_y >= SCREEN_HEIGHT) {
        scroll();
    }
}

void scroll() {
    if (cursor_y >= SCREEN_HEIGHT) {
        for (int y = 0; y < SCREEN_HEIGHT - 1; y++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                text_video_memory[y * SCREEN_WIDTH + x] = text_video_memory[(y + 1) * SCREEN_WIDTH + x];
            }
        }
        
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            text_video_memory[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH + x] = (text_color << 8) | ' ';
        }
        cursor_y = SCREEN_HEIGHT - 1;
    }
}

// Остальные функции остаются прежними...
void print(const char *str) {
    while (*str) {
        putchar(*str++);
    }
}

void printf(const char *format, ...) {
    print(format);
}

void set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

int get_cursor_x() { return cursor_x; }
int get_cursor_y() { return cursor_y; }