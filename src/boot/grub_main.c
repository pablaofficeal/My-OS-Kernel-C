#include "../drivers/serial.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include <stdint.h>

void kernel_main_grub(uint32_t magic, uint32_t *mbi) {
    (void)mbi;
    serial_init();
    serial_write_string("[GRUB] 64-bit long mode via multiboot2 trampoline\n");
    if(magic == 0x36d76289) serial_write_string("[GRUB] multiboot2 magic OK\n");
    else { serial_write_string("[GRUB] bad magic\n"); }

    gdt_init();
    serial_write_string("[GDT] loaded (GRUB path)\n");

    idt_init();
    serial_write_string("[IDT] loaded\n");

    __asm__ volatile("sti");
    serial_write_string("[INT] sti\n");
    serial_write_string("[TEST] int3\n");
    __asm__ volatile("int3");
    serial_write_string("[OK] int3 handled\n");

    // halt
    for(;;) __asm__ volatile("cli; hlt");
}
