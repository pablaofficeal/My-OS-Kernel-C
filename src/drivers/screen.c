// src/drivers/screen.c - Framebuffer-based graphics implementation
#include "screen.h"
#include "../lib/string.h"
#include "../lib/memory.h"

// Global framebuffer
uint32_t* framebuffer = (uint32_t*)0xFD000000;
int framebuffer_width = SCREEN_WIDTH;
int framebuffer_height = SCREEN_HEIGHT;
int framebuffer_pitch = SCREEN_WIDTH * BYTES_PER_PIXEL;

desktop_t* global_desktop = 0;  // Remove static keyword
static window_t* windows[MAX_WINDOWS];
static int window_count = 0;

// Font data (8x8 bitmap font)
unsigned char font_8x8[128][8] = {
    [32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
    [33] = {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // !
    [48] = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x66, 0x3C}, // 0
    [49] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E}, // 1
    [50] = {0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x66, 0x7E}, // 2
    [51] = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x06, 0x66, 0x3C}, // 3
    [52] = {0x06, 0x0E, 0x1E, 0x66, 0x7F, 0x06, 0x06, 0x0F}, // 4
    [53] = {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x66, 0x3C}, // 5
    [54] = {0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C}, // 6
    [55] = {0x7E, 0x66, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18}, // 7
    [56] = {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x66, 0x3C}, // 8
    [57] = {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x66, 0x3C}, // 9
    [65] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66}, // A
    [66] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x66, 0x7C}, // B
    [67] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x60, 0x66, 0x3C}, // C
    [68] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x66, 0x6C, 0x78}, // D
    [69] = {0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x7E}, // E
    [70] = {0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x60}, // F
    [71] = {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x66, 0x3C}, // G
    [72] = {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x66}, // H
    [73] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C}, // I
    [74] = {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x6C, 0x38}, // J
    [75] = {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x66}, // K
    [76] = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E}, // L
    [77] = {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x63}, // M
    [78] = {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x66}, // N
    [79] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C}, // O
    [80] = {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x60}, // P
    [81] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E}, // Q
    [82] = {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x66}, // R
    [83] = {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x06, 0x66, 0x3C}, // S
    [84] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18}, // T
    [85] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C}, // U
    [86] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x3C, 0x18}, // V
    [87] = {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x63}, // W
    [88] = {0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x66, 0x66}, // X
    [89] = {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x18}, // Y
    [90] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x7E}, // Z
    [97] = {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x66, 0x3E}, // a
    [98] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x7C}, // b
    [99] = {0x00, 0x00, 0x3C, 0x60, 0x60, 0x60, 0x60, 0x3C}, // c
    [100] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x7C}, // d
    [101] = {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x66, 0x3C}, // e
    [102] = {0x0C, 0x18, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18}, // f
    [103] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x7C}, // g
    [104] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66}, // h
    [105] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C}, // i
    [106] = {0x06, 0x00, 0x06, 0x06, 0x06, 0x06, 0x66, 0x3C}, // j
    [107] = {0x60, 0x60, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66}, // k
    [108] = {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C}, // l
    [109] = {0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x63, 0x63}, // m
    [110] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66}, // n
    [111] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C}, // o
    [112] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60}, // p
    [113] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06}, // q
    [114] = {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x60}, // r
    [115] = {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x66, 0x3C}, // s
    [116] = {0x0C, 0x18, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18}, // t
    [117] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C}, // u
    [118] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18}, // v
    [119] = {0x00, 0x00, 0x63, 0x63, 0x6B, 0x7F, 0x7F, 0x63}, // w
    [120] = {0x00, 0x00, 0x66, 0x66, 0x3C, 0x3C, 0x66, 0x66}, // x
    [121] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x0C, 0x78}, // y
    [122] = {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x60, 0x7E}, // z
};

// Mouse cursor data
static unsigned char mouse_cursor_data[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xC0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xE0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xF0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xF8, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFC, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFE, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xC0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xF0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xF8, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xFC, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xFE, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};

// ==================== FRAMEBUFFER FUNCTIONS ====================

int init_framebuffer(void) {
    // Allocate framebuffer memory
    framebuffer = (uint32_t*)0xFD000000; // Use high memory area
    
    if (!framebuffer) {
        return 0;
    }
    
    // Clear framebuffer
    framebuffer_clear(COLOR_BLACK);
    
    return 1;
}

void framebuffer_clear(uint32_t color) {
    if (framebuffer) {
        for (int i = 0; i < framebuffer_width * framebuffer_height; i++) {
            framebuffer[i] = color;
        }
    }
}

void framebuffer_put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < framebuffer_width && y >= 0 && y < framebuffer_height) {
        framebuffer[y * framebuffer_width + x] = color;
    }
}

uint32_t framebuffer_get_pixel(int x, int y) {
    if (x >= 0 && x < framebuffer_width && y >= 0 && y < framebuffer_height) {
        return framebuffer[y * framebuffer_width + x];
    }
    return 0;
}

void framebuffer_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t pixel = framebuffer_get_pixel(src_x + x, src_y + y);
            framebuffer_put_pixel(dst_x + x, dst_y + y, pixel);
        }
    }
}

void framebuffer_blend_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < framebuffer_width && y >= 0 && y < framebuffer_height) {
        uint32_t dst = framebuffer_get_pixel(x, y);
        
        // Extract alpha from source
        uint8_t src_alpha = (color >> 24) & 0xFF;
        
        if (src_alpha == 0xFF) {
            // Opaque - just copy
            framebuffer_put_pixel(x, y, color);
        } else if (src_alpha > 0) {
            // Alpha blending
            uint8_t dst_alpha = 0xFF - src_alpha;
            
            uint8_t src_r = (color >> 16) & 0xFF;
            uint8_t src_g = (color >> 8) & 0xFF;
            uint8_t src_b = color & 0xFF;
            
            uint8_t dst_r = (dst >> 16) & 0xFF;
            uint8_t dst_g = (dst >> 8) & 0xFF;
            uint8_t dst_b = dst & 0xFF;
            
            uint8_t final_r = (src_r * src_alpha + dst_r * dst_alpha) / 255;
            uint8_t final_g = (src_g * src_alpha + dst_g * dst_alpha) / 255;
            uint8_t final_b = (src_b * src_alpha + dst_b * dst_alpha) / 255;
            
            uint32_t final_color = 0xFF000000 | (final_r << 16) | (final_g << 8) | final_b;
            framebuffer_put_pixel(x, y, final_color);
        }
    }
}

// ==================== BASIC DRAWING FUNCTIONS ====================

void draw_pixel(int x, int y, uint32_t color) {
    framebuffer_put_pixel(x, y, color);
}

void draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        draw_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void draw_rect(int x, int y, int width, int height, uint32_t color) {
    // Top and bottom lines
    for (int i = x; i < x + width; i++) {
        draw_pixel(i, y, color);
        draw_pixel(i, y + height - 1, color);
    }
    
    // Left and right lines
    for (int i = y; i < y + height; i++) {
        draw_pixel(x, i, color);
        draw_pixel(x + width - 1, i, color);
    }
}

void fill_rect(int x, int y, int width, int height, uint32_t color) {
    for (int dy = y; dy < y + height; dy++) {
        for (int dx = x; dx < x + width; dx++) {
            draw_pixel(dx, dy, color);
        }
    }
}

void draw_circle(int cx, int cy, int radius, uint32_t color) {
    int x = radius;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        draw_pixel(cx + x, cy + y, color);
        draw_pixel(cx + y, cy + x, color);
        draw_pixel(cx - y, cy + x, color);
        draw_pixel(cx - x, cy + y, color);
        draw_pixel(cx - x, cy - y, color);
        draw_pixel(cx - y, cy - x, color);
        draw_pixel(cx + y, cy - x, color);
        draw_pixel(cx + x, cy - y, color);
        
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

void fill_circle(int cx, int cy, int radius, uint32_t color) {
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                draw_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color) {
    draw_line(x1, y1, x2, y2, color);
    draw_line(x2, y2, x3, y3, color);
    draw_line(x3, y3, x1, y1, color);
}

void fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color) {
    // Simple scanline fill algorithm
    int min_y = (y1 < y2) ? ((y1 < y3) ? y1 : y3) : ((y2 < y3) ? y2 : y3);
    int max_y = (y1 > y2) ? ((y1 > y3) ? y1 : y3) : ((y2 > y3) ? y2 : y3);
    
    for (int y = min_y; y <= max_y; y++) {
        int x_start = framebuffer_width;
        int x_end = 0;
        
        // Find intersections with triangle edges
        // Edge 1-2
        if ((y >= y1 && y <= y2) || (y >= y2 && y <= y1)) {
            if (y2 != y1) {
                int x = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
                if (x < x_start) x_start = x;
                if (x > x_end) x_end = x;
            }
        }
        // Edge 2-3
        if ((y >= y2 && y <= y3) || (y >= y3 && y <= y2)) {
            if (y3 != y2) {
                int x = x2 + (x3 - x2) * (y - y2) / (y3 - y2);
                if (x < x_start) x_start = x;
                if (x > x_end) x_end = x;
            }
        }
        // Edge 3-1
        if ((y >= y3 && y <= y1) || (y >= y1 && y <= y3)) {
            if (y1 != y3) {
                int x = x3 + (x1 - x3) * (y - y3) / (y1 - y3);
                if (x < x_start) x_start = x;
                if (x > x_end) x_end = x;
            }
        }
        
        // Draw scanline
        for (int x = x_start; x <= x_end; x++) {
            draw_pixel(x, y, color);
        }
    }
}

// ==================== TEXT RENDERING ====================

void draw_string(int x, int y, const char *str, uint32_t color) {
    int pos_x = x;
    while (*str) {
        draw_char(pos_x, y, *str, color);
        pos_x += 8;
        str++;
    }
}

int get_text_width(const char *str) {
    return strlen(str) * 8;
}

int get_char_width(void) {
    return 8;
}

int get_char_height(void) {
    return 8;
}

// ==================== WINDOW MANAGER ====================

static window_t* window_list[MAX_WINDOWS];

window_t* create_window(int x, int y, int width, int height, const char* title, uint32_t flags) {
    if (window_count >= MAX_WINDOWS) return 0;
    
    window_t* window = (window_t*)malloc(sizeof(window_t));
    if (!window) return 0;
    
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->flags = flags | WINDOW_FLAG_VISIBLE;
    // Copy title string manually
    int i;
    for (i = 0; i < 63 && title[i]; i++) {
        window->title[i] = title[i];
    }
    window->title[i] = '\0';
    
    // Allocate window buffer
    window->buffer = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!window->buffer) {
        free(window);
        return 0;
    }
    
    // Clear window buffer
    for (int i = 0; i < width * height; i++) {
        window->buffer[i] = COLOR_TRANSPARENT;
    }
    
    window->z_order = window_count;
    window->parent = 0;
    window->on_paint = 0;
    window->on_click = 0;
    window->on_key = 0;
    
    window_list[window_count++] = window;
    return window;
}

void destroy_window(window_t* window) {
    if (!window) return;
    
    // Remove from window list
    for (int i = 0; i < window_count; i++) {
        if (window_list[i] == window) {
            // Shift remaining windows
            for (int j = i; j < window_count - 1; j++) {
                window_list[j] = window_list[j + 1];
            }
            window_count--;
            break;
        }
    }
    
    if (window->buffer) {
        free(window->buffer);
    }
    free(window);
}

void show_window(window_t* window) {
    if (window) {
        window->flags |= WINDOW_FLAG_VISIBLE;
        window_paint(window);
    }
}

void hide_window(window_t* window) {
    if (window) {
        window->flags &= ~WINDOW_FLAG_VISIBLE;
        // Redraw desktop to remove window
        if (global_desktop) {
            desktop_paint(global_desktop);
        }
    }
}

void move_window(window_t* window, int x, int y) {
    if (!window) return;
    
    // Save old position
    int old_x = window->x;
    int old_y = window->y;
    
    // Update position
    window->x = x;
    window->y = y;
    
    // Redraw affected area
    if (global_desktop) {
        desktop_paint(global_desktop);
    }
}

void resize_window(window_t* window, int width, int height) {
    if (!window || width <= 0 || height <= 0) return;
    
    // Allocate new buffer
    uint32_t* new_buffer = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!new_buffer) return;
    
    // Clear new buffer
    for (int i = 0; i < width * height; i++) {
        new_buffer[i] = COLOR_TRANSPARENT;
    }
    
    // Copy old content (clipped)
    int copy_width = (width < window->width) ? width : window->width;
    int copy_height = (height < window->height) ? height : window->height;
    
    for (int y = 0; y < copy_height; y++) {
        for (int x = 0; x < copy_width; x++) {
            new_buffer[y * width + x] = window->buffer[y * window->width + x];
        }
    }
    
    // Free old buffer and update
    free(window->buffer);
    window->buffer = new_buffer;
    window->width = width;
    window->height = height;
    
    // Redraw
    if (global_desktop) {
        desktop_paint(global_desktop);
    }
}

void raise_window(window_t* window) {
    if (!window) return;
    
    // Find highest z-order
    int max_z = 0;
    for (int i = 0; i < window_count; i++) {
        if (window_list[i]->z_order > max_z) {
            max_z = window_list[i]->z_order;
        }
    }
    
    window->z_order = max_z + 1;
    
    // Redraw to reflect new order
    if (global_desktop) {
        desktop_paint(global_desktop);
    }
}

void lower_window(window_t* window) {
    if (!window) return;
    
    window->z_order = 0;
    
    // Redraw to reflect new order
    if (global_desktop) {
        desktop_paint(global_desktop);
    }
}

window_t* get_window_at_point(int x, int y) {
    window_t* found = 0;
    int highest_z = -1;
    
    for (int i = 0; i < window_count; i++) {
        window_t* win = window_list[i];
        if ((win->flags & WINDOW_FLAG_VISIBLE) && 
            x >= win->x && x < win->x + win->width &&
            y >= win->y && y < win->y + win->height &&
            win->z_order > highest_z) {
            found = win;
            highest_z = win->z_order;
        }
    }
    
    return found;
}

void window_paint(window_t* window) {
    if (!window || !(window->flags & WINDOW_FLAG_VISIBLE)) return;
    
    // Draw window decorations if decorated
    if (window->flags & WINDOW_FLAG_DECORATED) {
        // Draw shadow
        fill_rect(window->x + 3, window->y + 3, window->width, window->height, COLOR_DARK_GRAY);
        
        // Draw main window background
        fill_rect(window->x, window->y, window->width, window->height, COLOR_LIGHT_GRAY);
        
        // Draw title bar
        fill_rect(window->x, window->y, window->width, WINDOW_TITLE_HEIGHT, COLOR_BLUE);
        draw_string(window->x + 5, window->y + 8, window->title, COLOR_WHITE);
        
        // Draw window controls
        fill_rect(window->x + window->width - 45, window->y + 5, 15, 15, COLOR_RED);    // Close
        fill_rect(window->x + window->width - 25, window->y + 5, 15, 15, COLOR_YELLOW); // Minimize
        
        // Draw border
        draw_rect(window->x, window->y, window->width, window->height, COLOR_BLACK);
    }
    
    // Copy window buffer to screen
    for (int y = 0; y < window->height; y++) {
        for (int x = 0; x < window->width; x++) {
            uint32_t pixel = window->buffer[y * window->width + x];
            if (pixel != COLOR_TRANSPARENT) {
                framebuffer_blend_pixel(window->x + x, window->y + y, pixel);
            }
        }
    }
    
    // Call custom paint handler if available
    if (window->on_paint) {
        window->on_paint((struct window*)window);
    }
}

// ==================== DESKTOP MANAGER ====================

desktop_t* create_desktop(void) {
    desktop_t* desktop = (desktop_t*)malloc(sizeof(desktop_t));
    if (!desktop) return 0;
    
    desktop->window_count = 0;
    desktop->active_window = 0;
    desktop->mouse_x = SCREEN_WIDTH / 2;
    desktop->mouse_y = SCREEN_HEIGHT / 2;
    desktop->mouse_buttons = 0;
    desktop->desktop_color = 0xFF008080; // Teal background
    
    // Create desktop window
    desktop->desktop_window = create_window(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, "Desktop", 0);
    if (desktop->desktop_window) {
        desktop->desktop_window->z_order = -1; // Always at bottom
        fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, desktop->desktop_color);
    }
    
    global_desktop = desktop;
    return desktop;
}

void desktop_paint(desktop_t* desktop) {
    if (!desktop) return;
    
    // Clear desktop
    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, desktop->desktop_color);
    
    // Sort windows by z-order
    for (int i = 0; i < window_count - 1; i++) {
        for (int j = i + 1; j < window_count; j++) {
            if (window_list[i]->z_order > window_list[j]->z_order) {
                window_t* temp = window_list[i];
                window_list[i] = window_list[j];
                window_list[j] = temp;
            }
        }
    }
    
    // Paint all visible windows in z-order
    for (int i = 0; i < window_count; i++) {
        window_paint(window_list[i]);
    }
    
    // Draw mouse cursor
    draw_mouse_cursor(desktop->mouse_x, desktop->mouse_y);
}

void desktop_handle_mouse(desktop_t* desktop, int x, int y, int buttons) {
    if (!desktop) return;
    
    // Hide old cursor
    hide_mouse_cursor(desktop->mouse_x, desktop->mouse_y);
    
    // Update mouse position
    desktop->mouse_x = x;
    desktop->mouse_y = y;
    desktop->mouse_buttons = buttons;
    
    // Find window under mouse
    window_t* win = get_window_at_point(x, y);
    
    if (win && (buttons & 1)) { // Left button pressed
        // Make window active
        desktop->active_window = win;
        raise_window(win);
        
        // Call window's click handler
        if (win->on_click) {
            win->on_click((struct window*)win, x - win->x, y - win->y);
        }
    }
    
    // Redraw desktop
    desktop_paint(desktop);
}

void desktop_handle_key(desktop_t* desktop, char key) {
    if (!desktop || !desktop->active_window) return;
    
    // Send key to active window
    if (desktop->active_window->on_key) {
        desktop->active_window->on_key((struct window*)desktop->active_window, key);
    }
}

window_t* desktop_get_active_window(desktop_t* desktop) {
    return desktop ? desktop->active_window : 0;
}

// ==================== MOUSE CURSOR ====================

void draw_mouse_cursor(int x, int y) {
    for (int dy = 0; dy < CURSOR_HEIGHT; dy++) {
        for (int dx = 0; dx < CURSOR_WIDTH; dx++) {
            if (mouse_cursor_data[dy][dx] & (1 << (7 - dx % 8))) {
                draw_pixel(x + dx, y + dy, COLOR_WHITE);
            }
        }
    }
}

void hide_mouse_cursor(int x, int y) {
    // Redraw the area where cursor was
    // This is a simplified version - in real implementation we'd save background
    for (int dy = 0; dy < CURSOR_HEIGHT; dy++) {
        for (int dx = 0; dx < CURSOR_WIDTH; dx++) {
            if (mouse_cursor_data[dy][dx] & (1 << (7 - dx % 8))) {
                // In real implementation, we'd restore from saved background
                // For now, just redraw the desktop area
                if (global_desktop) {
                    draw_pixel(x + dx, y + dy, global_desktop->desktop_color);
                }
            }
        }
    }
}