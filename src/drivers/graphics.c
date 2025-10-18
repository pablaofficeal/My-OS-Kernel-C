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
    memset(framebuffer, 0, GRAPHICS_WIDTH * GRAPHICS_HEIGHT * 4);
    memset(back_buffer, 0, GRAPHICS_WIDTH * GRAPHICS_HEIGHT * 4);
    
    printf("Graphics: Ready! %dx%d, %dbpp\n", GRAPHICS_WIDTH, GRAPHICS_HEIGHT, BITS_PER_PIXEL);
}

// set_graphics_mode используется из screen.h
// Удален дубликат чтобы избежать конфликтов линковки

// Базовые графические функции - используются из screen.h
// Удалены дубликаты чтобы избежать конфликтов линковки

// Функции для оконного менеджера - используются из screen.h
// Удалены дубликаты чтобы избежать конфликтов линковки

void swap_buffers() {
    // Копируем back_buffer в framebuffer
    memcpy(framebuffer, back_buffer, GRAPHICS_WIDTH * GRAPHICS_HEIGHT * 4);
}

// Функции для оконного менеджера - используются из screen.h
// Удалены дубликаты чтобы избежать конфликтов линковки