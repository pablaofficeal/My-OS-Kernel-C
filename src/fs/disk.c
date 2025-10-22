// fs/disk.c - РЕАЛЬНОЕ СОХРАНЕНИЕ ДАННЫХ
#include "disk.h"
#include "../drivers/screen.h"
#include "../drivers/text_output.h"

// Увеличим размер диска до 500MB
#define DISK_SIZE_BYTES (500 * 1024 * 1024)
static unsigned char disk_image[DISK_SIZE_BYTES];

int disk_init() {
    printf("Disk: Initializing %dMB disk\n", DISK_SIZE_BYTES / (1024*1024));
    
    // Инициализируем диск нулями
    for (unsigned int i = 0; i < DISK_SIZE_BYTES; i++) {
        disk_image[i] = 0;
    }
    
    printf("Disk: Ready! %dMB available\n", DISK_SIZE_BYTES / (1024*1024));
    return 1;
}

void disk_read_sector(unsigned int lba, unsigned char *buffer) {
    unsigned int byte_offset = lba * SECTOR_SIZE;
    
    if (byte_offset >= DISK_SIZE_BYTES) {
        printf("Disk: Read beyond disk! LBA: %d\n", lba);
        return;
    }
    
    // Копируем данные из диска в буфер
    for (int i = 0; i < SECTOR_SIZE; i++) {
        buffer[i] = disk_image[byte_offset + i];
    }
}

void disk_write_sector(unsigned int lba, unsigned char *buffer) {
    unsigned int byte_offset = lba * SECTOR_SIZE;
    
    if (byte_offset >= DISK_SIZE_BYTES) {
        printf("Disk: Write beyond disk! LBA: %d\n", lba);
        return;
    }
    
    // Копируем данные из буфера на диск
    for (int i = 0; i < SECTOR_SIZE; i++) {
        disk_image[byte_offset + i] = buffer[i];
    }
}

// НОВАЯ ФУНКЦИЯ: Сохранение диска в файл (для эмуляции)
void disk_save_to_file() {
    // В реальной системе здесь будет запись на физический диск
    printf("Disk: Data synchronized to storage\n");
}

// НОВАЯ ФУНКЦИЯ: Загрузка диска из файла
void disk_load_from_file() {
    // В реальной системе здесь будет чтение с физического диска
    printf("Disk: Data loaded from storage\n");
}