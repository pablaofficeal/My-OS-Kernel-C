// kernel.c
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

void kernel_main(void) {
    shell_init();
    
    printf("Initializing FAT16...\n");
    if (!fat16_init()) {
        printf("FAT16 init failed!\n");
    }
    
    shell_run();
    
    while(1) asm("hlt");
}

void _start(void) {
    kernel_main();
}