#pragma once
#include <stdint.h>
#include <stdbool.h>

enum keyboard_special_key {
    KEYBOARD_SPECIAL_F1 = 1,
    KEYBOARD_SPECIAL_F2 = 2,
    KEYBOARD_SPECIAL_F3 = 3
};

void keyboard_init(void);
bool keyboard_has_key(void);
bool keyboard_try_getc(char *out); // non-blocking, returns true if key available
char keyboard_getc(void); // blocking poll
void keyboard_poll(void); // to be called in loop if IRQ not used
void keyboard_set_leds(bool caps, bool num, bool scroll);
bool keyboard_try_get_special(uint8_t *out);
