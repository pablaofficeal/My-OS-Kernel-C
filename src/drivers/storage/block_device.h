#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BLOCK_SECTOR_SIZE 512

bool block_device_init(void);
bool block_device_read(uint32_t lba, void *buffer);
bool block_device_write(uint32_t lba, const void *buffer);
const char *block_device_name(void);
