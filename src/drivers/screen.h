// src/drivers/screen.h
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

// Framebuffer configuration
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
#define BITS_PER_PIXEL 32
#define BYTES_PER_PIXEL 4

// Colors (ARGB format)
#define COLOR_TRANSPARENT 0x00000000
#define COLOR_BLACK       0xFF000000
#define COLOR_WHITE       0xFFFFFFFF
#define COLOR_RED         0xFFFF0000
#define COLOR_GREEN       0xFF00FF00
#define COLOR_BLUE        0xFF0000FF
#define COLOR_CYAN        0xFF00FFFF
#define COLOR_MAGENTA     0xFFFF00FF
#define COLOR_YELLOW      0xFFFFFF00
#define COLOR_GRAY        0xFF808080
#define COLOR_DARK_GRAY   0xFF404040
#define COLOR_LIGHT_GRAY  0xFFC0C0C0

// Window manager constants
#define MAX_WINDOWS 32
#define WINDOW_TITLE_HEIGHT 25
#define WINDOW_BORDER_WIDTH 2
#define TASKBAR_HEIGHT 30

// Window flags
#define WINDOW_FLAG_VISIBLE     0x01
#define WINDOW_FLAG_DECORATED   0x02
#define WINDOW_FLAG_RESIZABLE   0x04
#define WINDOW_FLAG_MINIMIZED   0x08
#define WINDOW_FLAG_MAXIMIZED   0x10

// Mouse cursor
#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 16

// Структура окна
typedef struct {
    int x, y;           // Position
    int width, height;  // Size
    uint32_t flags;     // Window flags
    char title[64];     // Window title
    uint32_t* buffer;   // Window buffer
    int z_order;        // Z-order (higher = on top)
    struct window* parent; // Parent window
    void (*on_paint)(struct window* win);
    void (*on_click)(struct window* win, int x, int y);
    void (*on_key)(struct window* win, char key);
} window_t;

// Структура desktop
typedef struct {
    window_t* windows[MAX_WINDOWS];
    int window_count;
    window_t* active_window;
    window_t* desktop_window;
    int mouse_x, mouse_y;
    int mouse_buttons;
    uint32_t desktop_color;
} desktop_t;

// Framebuffer functions
int init_framebuffer(void);
void framebuffer_clear(uint32_t color);
void framebuffer_put_pixel(int x, int y, uint32_t color);
uint32_t framebuffer_get_pixel(int x, int y);
void framebuffer_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int width, int height);
void framebuffer_blend_pixel(int x, int y, uint32_t color);

// Basic drawing functions
void draw_pixel(int x, int y, uint32_t color);
void draw_line(int x1, int y1, int x2, int y2, uint32_t color);
void draw_rect(int x, int y, int width, int height, uint32_t color);
void fill_rect(int x, int y, int width, int height, uint32_t color);
void draw_circle(int x, int y, int radius, uint32_t color);
void fill_circle(int x, int y, int radius, uint32_t color);
void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color);
void fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color);

// Text rendering
void draw_char(int x, int y, char c, uint32_t color);
void draw_string(int x, int y, const char *str, uint32_t color);
int get_text_width(const char *str);
int get_char_width(void);
int get_char_height(void);

// Window manager functions
window_t* create_window(int x, int y, int width, int height, const char* title, uint32_t flags);
void destroy_window(window_t* window);
void show_window(window_t* window);
void hide_window(window_t* window);
void move_window(window_t* window, int x, int y);
void resize_window(window_t* window, int width, int height);
void raise_window(window_t* window);
void lower_window(window_t* window);
window_t* get_window_at_point(int x, int y);
void window_paint(window_t* window);

// Desktop manager
desktop_t* create_desktop(void);
void desktop_paint(desktop_t* desktop);
void desktop_handle_mouse(desktop_t* desktop, int x, int y, int buttons);
void desktop_handle_key(desktop_t* desktop, char key);
window_t* desktop_get_active_window(desktop_t* desktop);

// Mouse cursor
void draw_mouse_cursor(int x, int y);
void hide_mouse_cursor(int x, int y);

// Global framebuffer pointer
extern uint32_t* framebuffer;
extern int framebuffer_width;
extern int framebuffer_height;
extern int framebuffer_pitch;

// Font data
extern unsigned char font_8x8[128][8];

// Global desktop pointer
extern desktop_t* global_desktop;

#endif