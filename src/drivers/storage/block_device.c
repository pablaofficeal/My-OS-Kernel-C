#include "block_device.h"
#include "ata_pio.h"

bool block_device_init(void){ return ata_pio_init(); }

bool block_device_read(uint32_t lba, void *buffer){
    return ata_pio_read_sector(lba,buffer);
}

bool block_device_write(uint32_t lba, const void *buffer){
    return ata_pio_write_sector(lba,buffer);
}

const char *block_device_name(void){ return ata_pio_device_name(); }
