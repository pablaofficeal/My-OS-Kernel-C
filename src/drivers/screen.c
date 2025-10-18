// src/drivers/screen.c
#include "screen.h"
#include "../lib/string.h"
#include "../lib/memory.h"

// Текстовый режим
volatile unsigned short *text_video_memory = (volatile unsigned short*)VIDEO_MEMORY;
int cursor_x = 0;
int cursor_y = 0;
unsigned char text_color = 0x07;

// Графический режим
int current_video_mode = MODE_TEXT;
uint32_t* graphics_framebuffer = 0;
vbe_mode_info_t* vbe_mode_info = 0;
int graphics_width = GRAPHICS_WIDTH;
int graphics_height = GRAPHICS_HEIGHT;

// ==================== ТЕКСТОВЫЙ РЕЖИМ ====================

void clear_screen() {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        text_video_memory[i] = (text_color << 8) | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
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

void putchar(char c) {
    if (current_video_mode == MODE_GRAPHICS) {
        // В графическом режиме пока используем текстовый вывод для отладки
        return;
    }
    
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
    scroll();
}

void print(const char *str) {
    while (*str) {
        putchar(*str++);
    }
}

void printf(const char *format, ...) {
    // Простая реализация - просто выводим строку
    print(format);
}

void set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

int get_cursor_x() { return cursor_x; }
int get_cursor_y() { return cursor_y; }

// ==================== ГРАФИЧЕСКИЙ РЕЖИМ ====================

// Простой шрифт 8x8
static unsigned char font_8x8[128][8] = {
    [32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
    [65] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66}, // A
    [66] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x66, 0x7C}, // B
    [67] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x60, 0x66, 0x3C}, // C
    // ... добавить остальные символы по необходимости
};

// Инициализация графического режима через VBE
int init_graphics() {
    printf("Initializing VBE graphics mode...\n");
    
    // В реальной системе здесь будет вызов VBE через BIOS или мультизагрузчик
    // Для эмуляции просто выделяем память под фреймбуфер
    
    graphics_framebuffer = (uint32_t*)0xFD000000; // Выделяем область памяти
    
    if (!graphics_framebuffer) {
        printf("Failed to allocate framebuffer!\n");
        return 0;
    }
    
    // Инициализируем чёрным экраном
    graphics_clear(COLOR_BLACK);
    
    printf("Graphics mode initialized: %dx%d, %dbpp\n", 
           graphics_width, graphics_height, BITS_PER_PIXEL);
    return 1;
}

void set_graphics_mode(int mode) {
    if (mode == MODE_GRAPHICS && current_video_mode != MODE_GRAPHICS) {
        if (init_graphics()) {
            current_video_mode = MODE_GRAPHICS;
            printf("Switched to graphics mode\n");
        }
    } else if (mode == MODE_TEXT && current_video_mode != MODE_TEXT) {
        current_video_mode = MODE_TEXT;
        clear_screen();
        printf("Switched to text mode\n");
    }
}

void put_pixel(int x, int y, uint32_t color) {
    if (graphics_framebuffer && x >= 0 && x < graphics_width && y >= 0 && y < graphics_height) {
        graphics_framebuffer[y * graphics_width + x] = color;
    }
}

void draw_rect(int x, int y, int width, int height, uint32_t color) {
    // Верхняя и нижняя линии
    for (int i = x; i < x + width; i++) {
        put_pixel(i, y, color);
        put_pixel(i, y + height - 1, color);
    }
    
    // Левая и правая линии
    for (int i = y; i < y + height; i++) {
        put_pixel(x, i, color);
        put_pixel(x + width - 1, i, color);
    }
}

void fill_rect(int x, int y, int width, int height, uint32_t color) {
    for (int dy = y; dy < y + height; dy++) {
        for (int dx = x; dx < x + width; dx++) {
            put_pixel(dx, dy, color);
        }
    }
}

void draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        put_pixel(x1, y1, color);
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

void draw_char(int x, int y, char c, uint32_t color) {
    unsigned char *glyph = font_8x8[(unsigned char)c];
    
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            if (glyph[dy] & (1 << (7 - dx))) {
                put_pixel(x + dx, y + dy, color);
            }
        }
    }
}

void draw_string(int x, int y, const char *str, uint32_t color) {
    int pos_x = x;
    while (*str) {
        draw_char(pos_x, y, *str, color);
        pos_x += 8;
        str++;
    }
}

void graphics_clear(uint32_t color) {
    if (graphics_framebuffer) {
        for (int i = 0; i < graphics_width * graphics_height; i++) {
            graphics_framebuffer[i] = color;
        }
    }
}

void graphics_printf(int x, int y, uint32_t color, const char *format, ...) {
    // Простая реализация - просто выводим строку
    draw_string(x, y, format, color);
}

// ==================== ОКОННЫЙ МЕНЕДЖЕР ====================

void draw_window(int x, int y, int width, int height, const char *title) {
    // Тень
    fill_rect(x + 3, y + 3, width, height, COLOR_DARK_GRAY);
    
    // Основное окно
    fill_rect(x, y, width, height, COLOR_GRAY);
    
    // Заголовок
    fill_rect(x, y, width, 25, COLOR_BLUE);
    draw_string(x + 5, y + 8, title, COLOR_WHITE);
    
    // Кнопки
    fill_rect(x + width - 45, y + 5, 15, 15, COLOR_RED);    // Закрыть
    fill_rect(x + width - 25, y + 5, 15, 15, COLOR_YELLOW); // Свернуть
    
    // Рамка
    draw_rect(x, y, width, height, COLOR_BLACK);
}

void draw_button(int x, int y, int width, int height, const char *label) {
    // Тень
    fill_rect(x + 2, y + 2, width, height, COLOR_DARK_GRAY);
    
    // Кнопка
    fill_rect(x, y, width, height, 0xFFC0C0C0); // Светло-серый
    
    // Текст
    int text_width = strlen(label) * 8;
    int text_x = x + (width - text_width) / 2;
    int text_y = y + (height - 8) / 2;
    draw_string(text_x, text_y, label, COLOR_BLACK);
    
    // Рамка
    draw_rect(x, y, width, height, COLOR_BLACK);
}