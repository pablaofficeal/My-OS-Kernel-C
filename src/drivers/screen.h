// src/drivers/screen.h - VGA driver header
#ifndef SCREEN_H
#define SCREEN_H

// I/O port helper, only available internally but declared here for safety
static inline void outb(unsigned short port, unsigned char value);

void clear_screen();
void putchar(char c);
void puts(const char* str);
void screen_init();

#endif