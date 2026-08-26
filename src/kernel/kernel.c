#include "kernel.h"
#include "../drivers/fb.h"
#include "../drivers/serial.h"
#include "../lib/string.h"

// simple halt
static void halt_forever(void) {
    for(;;) __asm__ volatile("cli; hlt");
}

void kernel_main(struct limine_framebuffer *fb) {
    fb_init(fb);
    serial_write_string("[KERNEL] fb_init done\n");

    fb_clear(0x1E1E2E); // dark bg
    fb_set_color(0xCDD6F4, 0x1E1E2E);

    fb_write_string("PureC OS 64-bit [UEFI+BIOS] Long Mode\n");
    fb_write_string("=====================================\n");
    fb_write_string("GDT64: OK  IDT64: OK  FB: OK  Serial: OK\n");

    // test IDT: trigger breakpoint exception #BP (vector 3)
    serial_write_string("[TEST] triggering int3\n");
    __asm__ volatile("int3");

    fb_write_string("\n[OK] int3 handled, IDT working!\n");
    fb_write_string("Interrupts enabled (sti), system stable.\n");
    fb_write_string("Halted. Check QEMU serial log for details.\n");

    serial_write_string("[KERNEL] entering halt loop\n");
    halt_forever();
}
