#pragma once

#include <stdbool.h>
#include <stdint.h>

struct i2c_hid_debug_state {
    bool controller_ready;
    bool descriptor_ready;
    bool device_ready;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t max_input_length;
    uint32_t transfer_errors;
    uint32_t input_reports;
    uint8_t last_report_id;
};

void i2c_hid_touchpad_init(uint64_t hhdm_offset);
void i2c_hid_touchpad_poll(void);
void i2c_hid_touchpad_redraw(void);
bool i2c_hid_touchpad_is_active(void);
struct i2c_hid_debug_state i2c_hid_touchpad_get_debug_state(void);
