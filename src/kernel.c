// kernel.c - ИСПРАВЛЕННАЯ ВЕРСИЯ
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS 0x00000003
#define MULTIBOOT_HEADER_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

__attribute__((section(".multiboot")))
__attribute__((aligned(4)))
const unsigned int multiboot_header[] = {
    MULTIBOOT_HEADER_MAGIC,
    MULTIBOOT_HEADER_FLAGS,
    MULTIBOOT_HEADER_CHECKSUM
};

#include "drivers/screen.h"
#include "shell/shell.h"
#include "fs/fat16.h"
#include "fs/disk.h"

// ОБЪЯВЛЯЕМ ФУНКЦИЮ ПЕРЕД ИСПОЛЬЗОВАНИЕМ
void kernel_main(void);

// Точка входа
void _start(void) {
    kernel_main();
}

// Реализация
void kernel_main(void) {
    shell_init();
    
    printf("Initializing disk...\n");
    disk_init();
    
    printf("Initializing FAT16...\n");
    if (!fat16_init()) {
        printf("FAT16 init failed!\n");
    }
    
    // Автоматическая синхронизация при запуске
    fat16_sync();
    
    printf("Kernel ready! Starting shell...\n");
    shell_run();
    
    // Синхронизация при завершении
    fat16_sync();
    
    while(1) asm("hlt");
}