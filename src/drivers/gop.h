#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../boot/limine.h"

// Универсальный GOP/Framebuffer драйвер
// Работает через Limine (UEFI) и как fallback через VGA text (BIOS)
struct gop_state {
    uint32_t *addr;
    uint32_t width, height, pitch;
    uint8_t bpp;
    bool available;
};

void gop_init_from_limine(struct limine_framebuffer *fb);
void gop_init_from_multiboot(void *mbi); // парсит multiboot2 info
bool gop_is_available(void);
uint32_t gop_get_width(void);
uint32_t gop_get_height(void);
void gop_clear(uint32_t color);
void gop_set_color(uint32_t fg, uint32_t bg);
void gop_putc(char c);
void gop_write(const char *s);
void gop_write_hex(uint64_t v);
void gop_draw_text_at(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
void gop_draw_text_sized_at(uint32_t x, uint32_t y, const char *text,
                            uint32_t fg, uint32_t bg, uint32_t size);
uint32_t gop_get_pixel(uint32_t x, uint32_t y);
void gop_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void gop_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gop_scroll_rect_up(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t amount, uint32_t fill_color);
void gop_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color);
