#include "limine.h"
#include <stddef.h>
#include <stdbool.h>
#include "../drivers/gop.h"
#include "../drivers/vga.h"
#include "../drivers/fb.h"
#include "../drivers/serial.h"
#include "../drivers/pic.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../kernel/syscall.h"
#include "../kernel/kernel.h"

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

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

    serial_init();
    serial_write_string("[BOOT] 64-bit long mode via Limine (UEFI/BIOS)\n");

    // HHDM для VGA 0xB8000 в higher half (иначе #PF и ребут)
    if(hhdm_request.response) vga_set_hhdm(hhdm_request.response->offset);
    else vga_set_hhdm(0);

    // GOP сначала, VGA только если GOP нет (иначе double init 0xB8000 ломает Limine)
    gop_init_from_limine(fb_ptr);
    if(gop_is_available()){
        gop_clear(0x1E1E2E); gop_set_color(0xCDD6F4,0x1E1E2E); gop_write("Limine GOP OK\n");
    } else {
        vga_init();
    }
    serial_write_string("[GOP] init done\n");

    pic_remap(0x20,0x28); pic_mask_all();
    serial_write_string("[PIC] remapped\n");

    gdt_init();
    serial_write_string("[GDT] loaded\n");

    idt_init();
    syscall_init();
    serial_write_string("[IDT] loaded\n");

    ps2_mouse_init();
    serial_write_string("[MOUSE] PS/2 ready\n");

    __asm__ volatile("sti");
    serial_write_string("[INT] sti enabled\n");

    kernel_main(fb_ptr);

    for(;;) __asm__ volatile("cli; hlt");
}
