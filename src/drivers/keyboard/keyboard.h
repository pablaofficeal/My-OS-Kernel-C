// drivers/keyboard.h
#ifndef KEYBOARD_H
#define KEYBOARD_H

char keyboard_getchar();
int keyboard_has_data();
void readline(char *buffer, int max_length);

#endif