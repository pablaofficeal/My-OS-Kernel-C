#pragma once

#include <stdint.h>

#define STORAGE_DEVICE_NAME_CAPACITY   9
#define STORAGE_MODEL_CAPACITY        41
#define STORAGE_SERIAL_CAPACITY       21

struct storage_device_info {
    char name[STORAGE_DEVICE_NAME_CAPACITY];
    char model[STORAGE_MODEL_CAPACITY];
    char serial[STORAGE_SERIAL_CAPACITY];
    uint64_t sector_count;
    uint32_t sector_size;
    uint8_t channel;
    uint8_t drive;
    uint8_t writable;
    uint8_t selected;
};
