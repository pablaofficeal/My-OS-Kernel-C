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

// Внешняя функция для получения Multiboot info
extern void* get_multiboot_info(void);

int init_framebuffer(void) {
    // Пытаемся получить информацию о framebuffer из Multiboot
    void* mb_info = get_multiboot_info();
    
    if (mb_info) {
        // Multiboot info structure
        typedef struct {
            uint32_t flags;
            uint32_t mem_lower;
            uint32_t mem_upper;
            uint32_t boot_device;
            uint32_t cmdline;
            uint32_t mods_count;
            uint32_t mods_addr;
            // Framebuffer info (если flags & 0x00001000)
            uint32_t fb_addr;
            uint32_t fb_pitch;
            uint32_t fb_width;
            uint32_t fb_height;
            uint8_t  fb_bpp;
            uint8_t  fb_type;
        } __attribute__((packed)) multiboot_info_t;
        
        multiboot_info_t* mbi = (multiboot_info_t*)mb_info;
        
        // Проверяем, есть ли информация о framebuffer
        if (mbi->flags & 0x00001000 && mbi->fb_addr != 0) {
            framebuffer = (uint32_t*)mbi->fb_addr;
            framebuffer_width = mbi->fb_width;
            framebuffer_height = mbi->fb_height;
            framebuffer_pitch = mbi->fb_pitch;
            
            // Очищаем framebuffer
            framebuffer_clear(COLOR_BLACK);
            return 1;
        }
    }
    
    // Fallback: без информации от Multiboot не можем безопасно инициализировать framebuffer
    // Возвращаем 0, чтобы использовать text mode вместо зависания
    return 0;
}

void framebuffer_clear(uint32_t color) {
    if (!framebuffer) return;
    
    // Очистка с использованием правильного pitch
    uint32_t* fb = framebuffer;
    int pitch_pixels = framebuffer_pitch / BYTES_PER_PIXEL;
    
    // Очищаем построчно с учетом pitch
    for (int y = 0; y < framebuffer_height; y++) {
        uint32_t* row = fb + y * pitch_pixels;
        for (int x = 0; x < framebuffer_width; x++) {
            row[x] = color;
        }
    }
}

void framebuffer_put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < framebuffer_width && y >= 0 && y < framebuffer_height) {
        // Используем pitch вместо width для правильного доступа к памяти
        framebuffer[(y * framebuffer_pitch / BYTES_PER_PIXEL) + x] = color;
    }
}

uint32_t framebuffer_get_pixel(int x, int y) {
    if (x >= 0 && x < framebuffer_width && y >= 0 && y < framebuffer_height) {
        int pitch_pixels = framebuffer_pitch / BYTES_PER_PIXEL;
        return framebuffer[y * pitch_pixels + x];
    }
    return 0;
}

void framebuffer_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int width, int height) {
    // Оптимизированное копирование прямоугольника
    if (src_x < 0 || src_y < 0 || dst_x < 0 || dst_y < 0) return;
    if (src_x + width > framebuffer_width || src_y + height > framebuffer_height) return;
    if (dst_x + width > framebuffer_width || dst_y + height > framebuffer_height) return;
    
    uint32_t* fb = framebuffer;
    int pitch_pixels = framebuffer_pitch / BYTES_PER_PIXEL;
    
    // Копируем построчно
    for (int y = 0; y < height; y++) {
        uint32_t* src_row = fb + (src_y + y) * pitch_pixels + src_x;
        uint32_t* dst_row = fb + (dst_y + y) * pitch_pixels + dst_x;
        
        // Копируем по 4 пикселя
        int x;
        for (x = 0; x < width - 3; x += 4) {
            dst_row[x] = src_row[x];
            dst_row[x + 1] = src_row[x + 1];
            dst_row[x + 2] = src_row[x + 2];
            dst_row[x + 3] = src_row[x + 3];
        }
        // Остальные пиксели
        for (; x < width; x++) {
            dst_row[x] = src_row[x];
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
    // Оптимизированный алгоритм Брезенхема
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    // Проверка границ
    if (x1 < 0 || x1 >= framebuffer_width || y1 < 0 || y1 >= framebuffer_height) return;
    if (x2 < 0 || x2 >= framebuffer_width || y2 < 0 || y2 >= framebuffer_height) return;
    
    uint32_t* fb = framebuffer;
    int pitch_pixels = framebuffer_pitch / BYTES_PER_PIXEL;
    
    while (1) {
        // Прямой доступ к framebuffer для скорости
        if (x1 >= 0 && x1 < framebuffer_width && y1 >= 0 && y1 < framebuffer_height) {
            fb[y1 * pitch_pixels + x1] = color;
        }
        
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
    // Проверка границ
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > framebuffer_width) width = framebuffer_width - x;
    if (y + height > framebuffer_height) height = framebuffer_height - y;
    if (width <= 0 || height <= 0) return;
    
    // Оптимизированное заполнение - рисуем целые строки
    uint32_t* fb = framebuffer;
    int pitch_pixels = framebuffer_pitch / BYTES_PER_PIXEL; // Pitch в пикселях
    
    for (int dy = y; dy < y + height; dy++) {
        uint32_t* row = fb + dy * pitch_pixels + x;
        // Заполняем строку по 4 пикселя
        int dx;
        for (dx = 0; dx < width - 3; dx += 4) {
            row[dx] = color;
            row[dx + 1] = color;
            row[dx + 2] = color;
            row[dx + 3] = color;
        }
        // Остальные пиксели в строке
        for (; dx < width; dx++) {
            row[dx] = color;
        }
    }
}

void draw_circle(int cx, int cy, int radius, uint32_t color) {
    // Оптимизированный алгоритм Брезенхема для круга
    int x = radius;
    int y = 0;
    int err = 0;
    
    uint32_t* fb = framebuffer;
    int pitch_pixels = framebuffer_pitch / BYTES_PER_PIXEL;
    
    while (x >= y) {
        // Рисуем 8 симметричных точек с прямой записью в framebuffer
        if (cx + x >= 0 && cx + x < framebuffer_width && cy + y >= 0 && cy + y < framebuffer_height)
            fb[(cy + y) * pitch_pixels + (cx + x)] = color;
        if (cx + y >= 0 && cx + y < framebuffer_width && cy + x >= 0 && cy + x < framebuffer_height)
            fb[(cy + x) * pitch_pixels + (cx + y)] = color;
        if (cx - y >= 0 && cx - y < framebuffer_width && cy + x >= 0 && cy + x < framebuffer_height)
            fb[(cy + x) * pitch_pixels + (cx - y)] = color;
        if (cx - x >= 0 && cx - x < framebuffer_width && cy + y >= 0 && cy + y < framebuffer_height)
            fb[(cy + y) * pitch_pixels + (cx - x)] = color;
        if (cx - x >= 0 && cx - x < framebuffer_width && cy - y >= 0 && cy - y < framebuffer_height)
            fb[(cy - y) * pitch_pixels + (cx - x)] = color;
        if (cx - y >= 0 && cx - y < framebuffer_width && cy - x >= 0 && cy - x < framebuffer_height)
            fb[(cy - x) * pitch_pixels + (cx - y)] = color;
        if (cx + y >= 0 && cx + y < framebuffer_width && cy - x >= 0 && cy - x < framebuffer_height)
            fb[(cy - x) * pitch_pixels + (cx + y)] = color;
        if (cx + x >= 0 && cx + x < framebuffer_width && cy - y >= 0 && cy - y < framebuffer_height)
            fb[(cy - y) * pitch_pixels + (cx + x)] = color;
        
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
    // Оптимизированное заполнение круга
    int radius_sq = radius * radius;
    int min_x = (cx - radius > 0) ? cx - radius : 0;
    int max_x = (cx + radius < framebuffer_width) ? cx + radius : framebuffer_width - 1;
    int min_y = (cy - radius > 0) ? cy - radius : 0;
    int max_y = (cy + radius < framebuffer_height) ? cy + radius : framebuffer_height - 1;
    
    uint32_t* fb = framebuffer;
    int pitch_pixels = framebuffer_pitch / BYTES_PER_PIXEL;
    
    for (int y = min_y; y <= max_y; y++) {
        int dy = y - cy;
        int dy_sq = dy * dy;
        uint32_t* row = fb + y * pitch_pixels;
        
        for (int x = min_x; x <= max_x; x++) {
            int dx = x - cx;
            if (dx * dx + dy_sq <= radius_sq) {
                row[x] = color;
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

// ==================== ADVANCED GRAPHICS FUNCTIONS ====================

// Градиентное заполнение прямоугольника
void fill_rect_gradient(int x, int y, int width, int height, uint32_t color1, uint32_t color2, int vertical) {
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > framebuffer_width) width = framebuffer_width - x;
    if (y + height > framebuffer_height) height = framebuffer_height - y;
    if (width <= 0 || height <= 0) return;
    
    // Извлекаем компоненты цветов
    uint8_t r1 = (color1 >> 16) & 0xFF;
    uint8_t g1 = (color1 >> 8) & 0xFF;
    uint8_t b1 = color1 & 0xFF;
    uint8_t r2 = (color2 >> 16) & 0xFF;
    uint8_t g2 = (color2 >> 8) & 0xFF;
    uint8_t b2 = color2 & 0xFF;
    
    uint32_t* fb = framebuffer;
    int pitch_pixels = framebuffer_pitch / BYTES_PER_PIXEL;
    
    if (vertical) {
        // Вертикальный градиент
        for (int dy = 0; dy < height; dy++) {
            int t = (dy * 255) / (height - 1);
            uint8_t r = r1 + ((r2 - r1) * t) / 255;
            uint8_t g = g1 + ((g2 - g1) * t) / 255;
            uint8_t b = b1 + ((b2 - b1) * t) / 255;
            uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
            
            uint32_t* row = fb + (y + dy) * pitch_pixels + x;
            for (int dx = 0; dx < width; dx++) {
                row[dx] = color;
            }
        }
    } else {
        // Горизонтальный градиент
        for (int dx = 0; dx < width; dx++) {
            int t = (dx * 255) / (width - 1);
            uint8_t r = r1 + ((r2 - r1) * t) / 255;
            uint8_t g = g1 + ((g2 - g1) * t) / 255;
            uint8_t b = b1 + ((b2 - b1) * t) / 255;
            uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
            
            for (int dy = 0; dy < height; dy++) {
                fb[(y + dy) * pitch_pixels + x + dx] = color;
            }
        }
    }
}

// Улучшенная отрисовка символа с антиалиасингом (упрощенная версия)
void draw_char_aa(int x, int y, char c, uint32_t color) {
    if (c < 32 || c >= 127) return;
    
    extern unsigned char font_8x8[128][8];
    unsigned char *char_data = font_8x8[(int)c];
    
    // Рисуем с небольшим размытием для антиалиасинга
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (char_data[row] & (1 << (7 - col))) {
                // Основной пиксель
                framebuffer_put_pixel(x + col, y + row, color);
                // Небольшое размытие для сглаживания
                if (col < 7 && !(char_data[row] & (1 << (6 - col)))) {
                    framebuffer_blend_pixel(x + col + 1, y + row, (color & 0xFFFFFF) | 0x80000000);
                }
                if (row < 7 && !(char_data[row + 1] & (1 << (7 - col)))) {
                    framebuffer_blend_pixel(x + col, y + row + 1, (color & 0xFFFFFF) | 0x80000000);
                }
            }
        }
    }
}

// Улучшенная отрисовка строки с антиалиасингом
void draw_string_aa(int x, int y, const char *str, uint32_t color) {
    int pos_x = x;
    for (int i = 0; str[i] != '\0'; i++) {
        draw_char_aa(pos_x, y, str[i], color);
        pos_x += 8; // Ширина символа
        if (pos_x >= framebuffer_width) break;
    }
}