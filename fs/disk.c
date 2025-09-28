// fs/disk.c - WORKING VERSION
#include "disk.h"
#include "../drivers/screen.h"

// Для простоты будем читать/писать в память вместо реального диска
unsigned char disk_image[1440 * 1024 ]; // 1.44MB

int disk_init() {
    printf("Disk: Initializing RAM disk\n");
    // В реальной ОС здесь будет чтение с реального диска
    // Пока просто инициализируем нулями
    for (int i = 0; i < sizeof(disk_image); i++) {
        disk_image[i] = 0;
    }
    return 1;
}

void disk_read_sector(unsigned int lba, unsigned char *buffer) {
    if (lba * SECTOR_SIZE >= sizeof(disk_image)) {
        printf("Disk: Read beyond disk size! LBA: %d\n", lba);
        return;
    }
    
    unsigned char *sector_start = disk_image + (lba * SECTOR_SIZE);
    for (int i = 0; i < SECTOR_SIZE; i++) {
        buffer[i] = sector_start[i];
    }
}

void disk_write_sector(unsigned int lba, unsigned char *buffer) {
    if (lba * SECTOR_SIZE >= sizeof(disk_image)) {
        printf("Disk: Write beyond disk size! LBA: %d\n", lba);
        return;
    }
    
    unsigned char *sector_start = disk_image + (lba * SECTOR_SIZE);
    for (int i = 0; i < SECTOR_SIZE; i++) {
        sector_start[i] = buffer[i];
    }
}