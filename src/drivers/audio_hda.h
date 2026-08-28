#pragma once

#include <stdbool.h>
#include <stdint.h>

struct hda_controller_info {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t output_streams;
    uint8_t input_streams;
    uint8_t bidirectional_streams;
    uint8_t major_version;
    uint8_t minor_version;
    bool mmio_ready;
};

void hda_init(void);
bool hda_is_present(void);
bool hda_pcm_output_ready(void);
bool hda_get_controller_info(struct hda_controller_info *out);
