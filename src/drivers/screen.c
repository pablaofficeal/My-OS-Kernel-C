// src/drivers/screen.c - VGA driver implementation
#include "screen.h"

// Custom types, no standard headers
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

// VGA constants
#define VGA_BUFFER 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
static uint16_t* vga_buffer;
static int cursor_x;
static int cursor_y;

// Update hardware cursor position
static void update_cursor() {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 14);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
}

// Helper to write to I/O ports
typedef unsigned char uint8_t;
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void screen_init() {
    // Disable interrupts before accessing VGA
    __asm__ __volatile__ ("cli");
    vga_buffer = (uint16_t*)VGA_BUFFER;
    cursor_x = 0;
    cursor_y = 0;
}

void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = 0x0700 | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

void putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = 0x0700 | c;
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    // Scroll screen if we reach the bottom
    if (cursor_y >= VGA_HEIGHT) {
        // Move all lines up by one
        for (int y = 1; y < VGA_HEIGHT; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[(y-1)*VGA_WIDTH + x] = vga_buffer[y*VGA_WIDTH + x];
            }
        }
        // Clear the last line
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(VGA_HEIGHT-1)*VGA_WIDTH + x] = 0x0700 | ' ';
        }
        cursor_y = VGA_HEIGHT - 1;
        cursor_x = 0;
    }
    update_cursor();
}

void puts(const char* str) {
    while (*str) {
        putchar(*str++);
    }
}