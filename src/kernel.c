// kernel.c - Core kernel, no drivers included
#include "drivers/screen.h"

// Function to stop the system FOREVER - NEVER returns!
__attribute__((noreturn)) void halt_forever() {
    while(1) {
        __asm__ __volatile__("cli; hlt");
    }
}

__attribute__((noreturn)) void kernel_main() {
    // Initialize screen driver FIRST
    screen_init();
    clear_screen();
    
    puts("PureC OS Kernel Loaded Successfully!\n");
    puts("===================================\n");
    puts("All drivers initialized, system stable.\n");
    
    // Halt forever - NO return, NO reboot
    halt_forever();
}