// drivers/graphics.h
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

// Режимы дисплея
#define VGA_TEXT_MODE 0
#define VGA_GRAPHICS_MODE 1

// Графические константы
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
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

// Структура точки
typedef struct {
    int x, y;
} point_t;

// Структура прямоугольника
typedef struct {
    int x, y;
    int width, height;
} rect_t;

// Структура цвета
typedef struct {
    uint8_t r, g, b, a;
} color_t;

// Функции графики
void graphics_init();
void set_graphics_mode(int mode);
void put_pixel(int x, int y, uint32_t color);
void draw_rect(int x, int y, int width, int height, uint32_t color);
void fill_rect(int x, int y, int width, int height, uint32_t color);
void draw_line(int x1, int y1, int x2, int y2, uint32_t color);
void draw_circle(int x, int y, int radius, uint32_t color);
void fill_circle(int x, int y, int radius, uint32_t color);
void draw_char(int x, int y, char c, uint32_t color);
void draw_string(int x, int y, const char *str, uint32_t color);
void clear_screen(uint32_t color);
void swap_buffers();

// Функции для оконного менеджера
void draw_window(int x, int y, int width, int height, const char *title);
void draw_button(int x, int y, int width, int height, const char *label);
void draw_menu_bar();

#endif