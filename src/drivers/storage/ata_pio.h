#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ata_pio_init(void);
bool ata_pio_read_sector(uint32_t lba, void *buffer);
bool ata_pio_write_sector(uint32_t lba, const void *buffer);
const char *ata_pio_device_name(void);
