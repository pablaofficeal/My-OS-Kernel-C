#pragma once

#include <stdint.h>

#define STORAGE_DEVICE_NAME_CAPACITY   9
#define STORAGE_MODEL_CAPACITY        41
#define STORAGE_SERIAL_CAPACITY       21
#define STORAGE_CONTROLLER_NAME_CAPACITY 8

#define STORAGE_CONTROLLER_AHCI 1
#define STORAGE_CONTROLLER_NVME 2

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

struct storage_controller_info {
    char name[STORAGE_CONTROLLER_NAME_CAPACITY];
    uint64_t register_base;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t type;
    uint8_t programming_interface;
};
