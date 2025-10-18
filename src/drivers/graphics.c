// drivers/graphics.c
#include "graphics.h"
#include "screen.h"
#include "../lib/string.h"
#include "../lib/memory.h"

// Видеобуферы
static uint32_t *framebuffer = (uint32_t*)0xFD000000; // Основной буфер
static uint32_t *back_buffer = (uint32_t*)0xFE000000; // Двойная буферизация

// Текущий режим
static int current_mode = VGA_TEXT_MODE;

// Шрифт 8x16 (упрощенный)
static unsigned char font_8x16[256][16] = {
    // Заглушка - в реальности нужен полный шрифт
    [32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, // пробел
    [65] = {0,0,24,24,36,36,36,126,66,66,66,66,0,0,0,0}, // A
    // ... остальные символы
};

void graphics_init() {
    printf("Graphics: Initializing framebuffer...\n");
    
    // Инициализируем буферы
    memset(framebuffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
    memset(back_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
    
    printf("Graphics: Ready! %dx%d, %dbpp\n", SCREEN_WIDTH, SCREEN_HEIGHT, BITS_PER_PIXEL);
}

void set_graphics_mode(int mode) {
    current_mode = mode;
    if (mode == VGA_GRAPHICS_MODE) {
        printf("Graphics: Switching to graphics mode\n");
        clear_screen(COLOR_BLACK);
    } else {
        printf("Graphics: Switching to text mode\n");
        clear_screen();
    }
}

void put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        back_buffer[y * SCREEN_WIDTH + x] = color;
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
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
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
    unsigned char *glyph = font_8x16[(unsigned char)c];
    
    for (int dy = 0; dy < 16; dy++) {
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

void clear_screen(uint32_t color) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        back_buffer[i] = color;
    }
}

void swap_buffers() {
    // Копируем back_buffer в framebuffer
    memcpy(framebuffer, back_buffer, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
}

// Функции для оконного менеджера
void draw_window(int x, int y, int width, int height, const char *title) {
    // Тень
    fill_rect(x + 4, y + 4, width, height, COLOR_DARK_GRAY);
    
    // Основное окно
    fill_rect(x, y, width, height, COLOR_GRAY);
    
    // Заголовок
    fill_rect(x, y, width, 20, COLOR_BLUE);
    draw_string(x + 5, y + 4, title, COLOR_WHITE);
    
    // Кнопки закрытия/свертывания
    fill_rect(x + width - 45, y + 3, 15, 14, COLOR_RED);
    fill_rect(x + width - 25, y + 3, 15, 14, COLOR_YELLOW);
    
    // Рамка
    draw_rect(x, y, width, height, COLOR_BLACK);
}

void draw_button(int x, int y, int width, int height, const char *label) {
    // Тень
    fill_rect(x + 2, y + 2, width, height, COLOR_DARK_GRAY);
    
    // Кнопка
    fill_rect(x, y, width, height, COLOR_GRAY);
    
    // Текст
    int text_x = x + (width - strlen(label) * 8) / 2;
    int text_y = y + (height - 16) / 2;
    draw_string(text_x, text_y, label, COLOR_BLACK);
    
    // Рамка
    draw_rect(x, y, width, height, COLOR_BLACK);
}

void draw_menu_bar() {
    fill_rect(0, 0, SCREEN_WIDTH, 25, COLOR_DARK_GRAY);
    draw_string(10, 5, "File Edit View Tools Help", COLOR_WHITE);
    
    // Время/системная информация
    char status[32];
    // sprintf(status, "Memory: %dMB Free", get_free_memory());
    draw_string(SCREEN_WIDTH - 150, 5, "PureC OS v2.0", COLOR_WHITE);
}