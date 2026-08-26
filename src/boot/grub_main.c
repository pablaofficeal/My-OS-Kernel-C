#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "../drivers/pic.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include <stdint.h>

void kernel_main_grub(uint32_t magic, uint32_t *mbi) {
    (void)mbi;
    vga_init();
    vga_write("PureC OS 64-bit [GRUB]\n");
    serial_init();
    serial_write_string("[GRUB] 64-bit long mode via multiboot2 trampoline\n");
    vga_write("[GRUB] long mode OK\n");
    if(magic == 0x36d76289){ serial_write_string("[GRUB] multiboot2 magic OK\n"); vga_write("multiboot2 OK\n"); }
    else { serial_write_string("[GRUB] bad magic\n"); vga_write("bad magic\n"); }

    // Важно для VirtualBox: перемапить PIC до STI, иначе IRQ0 прилетит как #GP
    pic_remap(0x20, 0x28);
    pic_mask_all();
    serial_write_string("[PIC] remapped to 0x20/0x28, masked\n");

    gdt_init();
    serial_write_string("[GDT] loaded (GRUB path)\n");
    vga_write("[GDT] OK\n");

    idt_init();
    serial_write_string("[IDT] loaded\n");
    vga_write("[IDT] OK\n");

    __asm__ volatile("sti");
    serial_write_string("[INT] sti\n");
    vga_write("[INT] sti\n");
    serial_write_string("[TEST] int3\n");
    vga_write("int3 test...\n");
    __asm__ volatile("int3");
    serial_write_string("[OK] int3 handled\n");
    vga_write("[OK] int3 handled - system stable, halted.\n");

    // halt
    for(;;) __asm__ volatile("cli; hlt");
}
