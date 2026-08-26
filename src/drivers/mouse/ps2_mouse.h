#pragma once
#include <stdint.h>
#include <stdbool.h>

struct mouse_state {
    int32_t x, y;
    int32_t dx, dy;
    uint8_t buttons; // bit0 left, bit1 right, bit2 middle
    bool has_data;
};

void ps2_mouse_init(void);
void ps2_mouse_handler(void); // вызывается из IRQ12
struct mouse_state mouse_get_state(void);
void mouse_set_bounds(int32_t w, int32_t h);
