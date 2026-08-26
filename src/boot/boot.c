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
#include "../kernel/klog.h"

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

    // HHDM для VGA 0xB8000 в higher half (иначе #PF и ребут)
    if(hhdm_request.response) vga_set_hhdm(hhdm_request.response->offset);
    else vga_set_hhdm(0);

    // GOP сначала, VGA только если GOP нет
    gop_init_from_limine(fb_ptr);

    // первичный экран как в Linux: показываем всё по дефолту
    klog_init();
    klog(KLOG_OK, "Limine boot: 64-bit long mode, paging enabled");
    klogf(KLOG_INFO, "HHDM offset: 0x%llx", hhdm_request.response ? hhdm_request.response->offset : 0);
    if(gop_is_available()){
        klogf(KLOG_OK, "GOP initialized: %dx%d bpp=%d", fb_ptr->width, fb_ptr->height, fb_ptr->bpp);
    } else {
        klog(KLOG_WARN, "GOP unavailable, fallback to VGA text 80x25");
    }

    klog(KLOG_INFO, "Initializing PIC...");
    pic_remap(0x20,0x28); pic_mask_all();
    klog(KLOG_OK, "PIC remapped: master 0x20 slave 0x28, all masked");

    klog(KLOG_INFO, "Loading GDT...");
    gdt_init();
    klog(KLOG_OK, "GDT loaded (null, code64 0x9A, data 0x92)");

    klog(KLOG_INFO, "Loading IDT...");
    idt_init();
    syscall_init();
    klog(KLOG_OK, "IDT loaded (256 vectors, ist=0, DPL3 for 0x80)");

    klog(KLOG_INFO, "Initializing PS/2 mouse...");
    ps2_mouse_init();
    klog(KLOG_OK, "PS/2 mouse ready (IRQ12)");

    klog(KLOG_INFO, "Enabling interrupts...");
    __asm__ volatile("cli");
    klog(KLOG_DEBUG, "CLI executed, preparing STI");
    __asm__ volatile("sti");
    klog(KLOG_OK, "Interrupts enabled (STI)");

    kernel_main(fb_ptr);

    for(;;) __asm__ volatile("cli; hlt");
}
