// Simple text output implementation for kernel
#include "text_output.h"
#include "screen.h"

// Text mode cursor position
static int cursor_x = 0;
static int cursor_y = 0;
static int text_color = 0xFFFFFF; // White

// VGA text mode buffer (80x25 characters)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

// Simple framebuffer text output
void set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

void clear_screen(void) {
    // Clear framebuffer to black
    if (framebuffer) {
        framebuffer_clear(0x000000);
    }
    cursor_x = 0;
    cursor_y = 0;
}

int putchar(int c) {
    // Handle special characters
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = VGA_HEIGHT - 1;
            // Simple scroll - just clear last line for now
            for (int x = 0; x < VGA_WIDTH; x++) {
                // Draw space character at bottom line
                draw_char(x * 8, cursor_y * 16, ' ', text_color);
            }
        }
        return c;
    }
    
    if (c == '\r') {
        cursor_x = 0;
        return c;
    }
    
    if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
        return c;
    }
    
    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            draw_char(cursor_x * 8, cursor_y * 16, ' ', text_color);
        }
        return c;
    }
    
    // Regular character
    if (c >= 32 && c < 127) {
        draw_char(cursor_x * 8, cursor_y * 16, c, text_color);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= VGA_HEIGHT) {
                cursor_y = VGA_HEIGHT - 1;
            }
        }
    }
    
    return c;
}

int printf(const char *format, ...) {
    // Simple printf implementation - just handle %s, %d, %x for now
    const char *p = format;
    va_list args;
    va_start(args, format);
    
    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            switch (*p) {
                case 's': {
                    char *str = va_arg(args, char*);
                    while (*str) {
                        putchar(*str++);
                    }
                    break;
                }
                case 'd': {
                    int val = va_arg(args, int);
                    if (val < 0) {
                        putchar('-');
                        val = -val;
                    }
                    // Simple decimal conversion
                    char buffer[12];
                    int i = 0;
                    if (val == 0) {
                        buffer[i++] = '0';
                    } else {
                        while (val > 0) {
                            buffer[i++] = '0' + (val % 10);
                            val /= 10;
                        }
                    }
                    while (i > 0) {
                        putchar(buffer[--i]);
                    }
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    putchar('0');
                    putchar('x');
                    // Simple hex conversion
                    char buffer[8];
                    int i = 0;
                    if (val == 0) {
                        buffer[i++] = '0';
                    } else {
                        while (val > 0) {
                            int digit = val & 0xF;
                            buffer[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
                            val >>= 4;
                        }
                    }
                    while (i > 0) {
                        putchar(buffer[--i]);
                    }
                    break;
                }
                case 'X': {
                    unsigned int val = va_arg(args, unsigned int);
                    // Uppercase hex
                    char buffer[8];
                    int i = 0;
                    if (val == 0) {
                        buffer[i++] = '0';
                    } else {
                        while (val > 0) {
                            int digit = val & 0xF;
                            buffer[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
                            val >>= 4;
                        }
                    }
                    while (i > 0) {
                        putchar(buffer[--i]);
                    }
                    break;
                }
                case 'c': {
                    int val = va_arg(args, int);
                    putchar(val);
                    break;
                }
                default:
                    putchar('%');
                    putchar(*p);
                    break;
            }
        } else {
            putchar(*p);
        }
        p++;
    }
    
    va_end(args);
    return 0;
}

// Helper function to draw a character using the framebuffer
void draw_char(int x, int y, char c, uint32_t color) {
    if (c < 32 || c >= 127) return; // Only printable ASCII
    
    // Use the 8x8 font data from screen.c
    extern unsigned char font_8x8[128][8];
    unsigned char *char_data = font_8x8[(int)c];
    
    // Draw 8x8 character
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (char_data[row] & (1 << (7 - col))) {
                framebuffer_put_pixel(x + col, y + row, color);
            }
        }
    }
}