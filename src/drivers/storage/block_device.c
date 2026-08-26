#include "block_device.h"
#include "ata_pio.h"

bool block_device_init(void){ return ata_pio_init(); }

uint32_t block_device_count(void){
    (void)ata_pio_init();
    return ata_pio_device_count();
}

int32_t block_device_list(struct storage_device_info *devices, uint32_t capacity){
    if(!devices || capacity==0 || capacity>0x7FFFFFFF) return -1;
    (void)ata_pio_init();
    uint32_t count=ata_pio_device_count();
    if(count>capacity) count=capacity;
    for(uint32_t index=0;index<count;index++){
        if(!ata_pio_get_device_info(index,&devices[index])) return -1;
    }
    return (int32_t)count;
}

bool block_device_select(uint32_t index){ return ata_pio_select_device(index); }

bool block_device_read(uint32_t lba, void *buffer){
    return ata_pio_read_sector(lba,buffer);
}

bool block_device_write(uint32_t lba, const void *buffer){
    return ata_pio_write_sector(lba,buffer);
}

const char *block_device_name(void){ return ata_pio_device_name(); }
