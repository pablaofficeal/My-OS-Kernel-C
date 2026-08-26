#include "block_device.h"
#include "ahci.h"
#include "ata_pio.h"

bool block_device_init(void){
    bool ata_available=ata_pio_init();
    bool ahci_available=ahci_init(ata_pio_device_count());
    return ata_available || ahci_available;
}

uint32_t block_device_count(void){
    (void)ata_pio_init();
    return ata_pio_device_count();
}

int32_t block_device_list(struct storage_device_info *devices, uint32_t capacity){
    if(!devices || capacity==0 || capacity>0x7FFFFFFF) return -1;
    (void)block_device_init();
    uint32_t ata_count=ata_pio_device_count();
    uint32_t ahci_count=ahci_device_count();
    uint32_t count=0;
    for(uint32_t index=0;index<ata_count && count<capacity;index++){
        if(!ata_pio_get_device_info(index,&devices[index])) return -1;
        count++;
    }
    for(uint32_t index=0;index<ahci_count && count<capacity;index++){
        if(!ahci_get_device_info(index,&devices[count])) return -1;
        count++;
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
