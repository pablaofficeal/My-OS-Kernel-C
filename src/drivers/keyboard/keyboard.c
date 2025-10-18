// drivers/keyboard.c - FIXED KEYCODES
#include "keyboard.h"
#include "../drivers/screen.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Правильная таблица сканкодов (US QWERTY)
static unsigned char scancodes[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static unsigned char shift_scancodes[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static int shift_pressed = 0;
static int caps_lock = 0;

int keyboard_has_data() {
    unsigned char status;
    asm volatile ("inb %%dx, %%al" : "=a"(status) : "d"((unsigned short)KEYBOARD_STATUS_PORT));
    return (status & 1);
}

unsigned char keyboard_read_scancode() {
    unsigned char data;
    asm volatile ("inb %%dx, %%al" : "=a"(data) : "d"((unsigned short)KEYBOARD_DATA_PORT));
    return data;
}

char scancode_to_char(unsigned char scancode) {
    // Key release
    if (scancode & 0x80) {
        unsigned char key = scancode & 0x7F;
        if (key == 0x2A || key == 0x36) shift_pressed = 0;
        return 0;
    }
    
    // Key press
    switch (scancode) {
        case 0x2A: // Left shift
        case 0x36: // Right shift
            shift_pressed = 1;
            return 0;
        case 0x3A: // Caps lock
            caps_lock = !caps_lock;
            return 0;
        case 0x1C: // Enter
            return '\n';
        case 0x0E: // Backspace
            return '\b';
        case 0x0F: // Tab
            return '\t';
        case 0x39: // Space
            return ' ';
        case 0x01: // Escape
            return 27;
    }
    
    // Regular keys
    if (scancode < sizeof(scancodes)) {
        if (shift_pressed || caps_lock) {
            return shift_scancodes[scancode];
        } else {
            return scancodes[scancode];
        }
    }
    
    return 0;
}

int kbhit() {
    return keyboard_has_data();
}

char getchar() {
    while (1) {
        if (kbhit()) {
            unsigned char scancode = keyboard_read_scancode();
            char c = scancode_to_char(scancode);
            if (c != 0) {
                // Отладочный вывод (можно убрать потом)
                // printf("[scancode: 0x%02x -> '%c']", scancode, c);
                return c;
            }
        }
        // Увеличиваем задержку для стабильности
        for (volatile int i = 0; i < 5000; i++);
    }
}

void readline(char *buffer, int max_length) {
    int pos = 0;
    
    while (1) {
        char c = getchar();
        
        if (c == '\n') {
            putchar('\n');
            buffer[pos] = '\0';
            return;
        } else if (c == '\b') {
            if (pos > 0) {
                putchar('\b');
                putchar(' ');
                putchar('\b');
                pos--;
            }
        } else if (c == 27) { // ESC
            buffer[0] = '\0';
            return;
        } else if (c >= 32 && c < 127 && pos < max_length - 1) {
            putchar(c);
            buffer[pos++] = c;
        }
    }
}