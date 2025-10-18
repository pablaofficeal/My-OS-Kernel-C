// src/drivers/screen.h
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

// Текстовый режим VGA
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define VIDEO_MEMORY 0xB8000

// Графический режим VBE
#define VBE_MODE 0x118  // 1024x768x32
#define GRAPHICS_WIDTH 1024
#define GRAPHICS_HEIGHT 768
#define BITS_PER_PIXEL 32

// Цвета (ARGB)
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_CYAN      0xFF00FFFF
#define COLOR_MAGENTA   0xFFFF00FF
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_GRAY      0xFF808080
#define COLOR_DARK_GRAY 0xFF404040

// Режимы дисплея
#define MODE_TEXT 0
#define MODE_GRAPHICS 1

// Структуры VBE
typedef struct {
    uint16_t attributes;
    uint8_t window_a, window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a, segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch; // bytes per scanline
    
    uint16_t width, height;
    uint8_t w_char, y_char, planes, bpp, banks;
    uint8_t memory_model, bank_size, image_pages;
    uint8_t reserved0;
    
    uint8_t red_mask, red_position;
    uint8_t green_mask, green_position;
    uint8_t blue_mask, blue_position;
    uint8_t reserved_mask, reserved_position;
    uint8_t direct_color_attributes;
    
    uint32_t framebuffer;
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
    uint8_t reserved1[206];
} __attribute__((packed)) vbe_mode_info_t;

// Функции текстового режима
void clear_screen();
void putchar(char c);
void print(const char *str);
void printf(const char *format, ...);
void set_cursor(int x, int y);
int get_cursor_x();
int get_cursor_y();

// Функции графического режима
int init_graphics();
void set_graphics_mode(int mode);
void put_pixel(int x, int y, uint32_t color);
void draw_rect(int x, int y, int width, int height, uint32_t color);
void fill_rect(int x, int y, int width, int height, uint32_t color);
void draw_line(int x1, int y1, int x2, int y2, uint32_t color);
void draw_char(int x, int y, char c, uint32_t color);
void draw_string(int x, int y, const char *str, uint32_t color);
void graphics_clear(uint32_t color);
void graphics_printf(int x, int y, uint32_t color, const char *format, ...);

// Функции оконного менеджера
void draw_window(int x, int y, int width, int height, const char *title);
void draw_button(int x, int y, int width, int height, const char *label);

// Глобальные переменные
extern int current_video_mode;
extern uint32_t* graphics_framebuffer;

#endif