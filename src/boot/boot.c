#include "limine.h"
#include <stddef.h>
#include <stdbool.h>
#include "../drivers/fb.h"
#include "../drivers/serial.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../kernel/kernel.h"

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

// for fb/serial access from other units
struct limine_framebuffer *fb_ptr = 0;

void _start(void) {
    // Limine already in 64-bit long mode, paging enabled
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        for(;;) __asm__ volatile("hlt");
    }
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        for(;;) __asm__ volatile("hlt");
    }

    fb_ptr = framebuffer_request.response->framebuffers[0];

    // init serial early for QEMU -serial debugging
    serial_init();
    serial_write_string("[BOOT] 64-bit long mode entered via Limine (UEFI/BIOS)\n");

    // Init GDT64
    gdt_init();
    serial_write_string("[GDT] loaded\n");

    // Init IDT64
    idt_init();
    serial_write_string("[IDT] loaded\n");

    // Enable interrupts
    __asm__ volatile("sti");
    serial_write_string("[INT] sti enabled\n");

    kernel_main(fb_ptr);

    for(;;) __asm__ volatile("cli; hlt");
}
