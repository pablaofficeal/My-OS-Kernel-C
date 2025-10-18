// kernel.c - ОБНОВЛЕННЫЙ
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

void kernel_main(void);

void _start(void) {
    kernel_main();
}

void kernel_main(void) {
    shell_init();
    
    // Инициализация графики ДО файловой системы
    printf("Initializing graphics...\n");
    init_graphics();
    
    printf("Initializing disk...\n");
    disk_init();
    
    printf("Initializing FAT16...\n");
    if (!fat16_init()) {
        printf("FAT16 init failed!\n");
    }
    
    fat16_sync();
    
    printf("Kernel ready! Starting shell...\n");
    printf("Try: graphics, desktop, textmode commands\n");
    
    shell_run();
    
    fat16_sync();
    
    while(1) asm("hlt");
}