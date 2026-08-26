#include "ata_pio.h"

#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_FEATURES   1
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LOW    3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HIGH   5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

#define ATA_STATUS_ERROR   0x01
#define ATA_STATUS_DRQ     0x08
#define ATA_STATUS_FAULT   0x20
#define ATA_STATUS_BUSY    0x80

#define ATA_CMD_READ       0x20
#define ATA_CMD_WRITE      0x30
#define ATA_CMD_FLUSH      0xE7
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_POLL_LIMIT     1000000

struct ata_device {
    uint16_t io_base;
    uint16_t control_base;
    uint8_t drive;
    const char *name;
    bool available;
};

static struct ata_device active_device;

static inline void outb(uint16_t port, uint8_t value){
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port){
    uint16_t value;
    __asm__ volatile("inw %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value){
    __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));
}

static void delay_400ns(const struct ata_device *device){
    for(uint8_t index=0;index<4;index++) (void)inb(device->control_base);
}

static bool wait_not_busy(const struct ata_device *device){
    for(uint32_t attempt=0;attempt<ATA_POLL_LIMIT;attempt++){
        uint8_t status=inb(device->io_base+ATA_REG_STATUS);
        if(status==0xFF) return false;
        if(!(status&ATA_STATUS_BUSY)) return true;
    }
    return false;
}

static bool wait_for_data(const struct ata_device *device){
    for(uint32_t attempt=0;attempt<ATA_POLL_LIMIT;attempt++){
        uint8_t status=inb(device->io_base+ATA_REG_STATUS);
        if(status==0 || status==0xFF) return false;
        if(status&(ATA_STATUS_ERROR|ATA_STATUS_FAULT)) return false;
        if(!(status&ATA_STATUS_BUSY) && (status&ATA_STATUS_DRQ)) return true;
    }
    return false;
}

static bool wait_for_completion(const struct ata_device *device){
    if(!wait_not_busy(device)) return false;
    uint8_t status=inb(device->io_base+ATA_REG_STATUS);
    return status!=0 && status!=0xFF
        && !(status&(ATA_STATUS_ERROR|ATA_STATUS_FAULT));
}

static bool identify_device(struct ata_device *device){
    outb(device->control_base,2);
    outb(device->io_base+ATA_REG_DRIVE,device->drive ? 0xB0 : 0xA0);
    delay_400ns(device);
    outb(device->io_base+ATA_REG_SECCOUNT,0);
    outb(device->io_base+ATA_REG_LBA_LOW,0);
    outb(device->io_base+ATA_REG_LBA_MID,0);
    outb(device->io_base+ATA_REG_LBA_HIGH,0);
    outb(device->io_base+ATA_REG_COMMAND,ATA_CMD_IDENTIFY);

    uint8_t status=inb(device->io_base+ATA_REG_STATUS);
    if(status==0 || status==0xFF) return false;
    if(!wait_not_busy(device)) return false;
    if(inb(device->io_base+ATA_REG_LBA_MID)!=0
       || inb(device->io_base+ATA_REG_LBA_HIGH)!=0) return false;
    if(!wait_for_data(device)) return false;

    for(uint16_t word=0;word<256;word++) (void)inw(device->io_base+ATA_REG_DATA);
    device->available=true;
    return true;
}

bool ata_pio_init(void){
    static const struct ata_device candidates[] = {
        {0x1F0,0x3F6,0,"primary master",false},
        {0x1F0,0x3F6,1,"primary slave",false},
        {0x170,0x376,0,"secondary master",false},
        {0x170,0x376,1,"secondary slave",false}
    };

    if(active_device.available) return true;
    for(uint8_t index=0;index<sizeof(candidates)/sizeof(candidates[0]);index++){
        struct ata_device candidate=candidates[index];
        if(identify_device(&candidate)){
            active_device=candidate;
            return true;
        }
    }
    return false;
}

static bool select_lba(uint32_t lba, uint8_t command){
    if(!active_device.available || lba>0x0FFFFFFF) return false;
    if(!wait_not_busy(&active_device)) return false;

    outb(active_device.io_base+ATA_REG_DRIVE,
         (uint8_t)(0xE0|(active_device.drive<<4)|((lba>>24)&0x0F)));
    delay_400ns(&active_device);
    outb(active_device.io_base+ATA_REG_FEATURES,0);
    outb(active_device.io_base+ATA_REG_SECCOUNT,1);
    outb(active_device.io_base+ATA_REG_LBA_LOW,(uint8_t)lba);
    outb(active_device.io_base+ATA_REG_LBA_MID,(uint8_t)(lba>>8));
    outb(active_device.io_base+ATA_REG_LBA_HIGH,(uint8_t)(lba>>16));
    outb(active_device.io_base+ATA_REG_COMMAND,command);
    return wait_for_data(&active_device);
}

bool ata_pio_read_sector(uint32_t lba, void *buffer){
    if(!buffer || !select_lba(lba,ATA_CMD_READ)) return false;
    uint16_t *words=(uint16_t*)buffer;
    for(uint16_t index=0;index<256;index++) words[index]=inw(active_device.io_base+ATA_REG_DATA);
    delay_400ns(&active_device);
    return true;
}

bool ata_pio_write_sector(uint32_t lba, const void *buffer){
    if(!buffer || !select_lba(lba,ATA_CMD_WRITE)) return false;
    const uint16_t *words=(const uint16_t*)buffer;
    for(uint16_t index=0;index<256;index++) outw(active_device.io_base+ATA_REG_DATA,words[index]);

    outb(active_device.io_base+ATA_REG_COMMAND,ATA_CMD_FLUSH);
    return wait_for_completion(&active_device);
}

const char *ata_pio_device_name(void){
    return active_device.available ? active_device.name : "none";
}
