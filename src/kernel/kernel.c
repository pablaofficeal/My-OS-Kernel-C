#include "kernel.h"
#include "../drivers/gop.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../kernel/syscall.h"
#include "../kernel/klog.h"
#include "../lib/string.h"

static inline int64_t do_syscall(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5){
    int64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "r10","r8","memory");
    return ret;
}

static void idle_forever(void){
    __asm__ volatile("sti");
    for(;;){
        ps2_mouse_poll();
        __asm__ volatile("pause");
    }
}

void kernel_main(struct limine_framebuffer *fb) {
    (void)fb;
    klog(KLOG_INFO, "Entering kernel_main...");
    klog(KLOG_INFO, "Kernel: 64-bit long mode, GOP active");
    klogf(KLOG_INFO, "Framebuffer: %dx%d", gop_get_width(), gop_get_height());

    // GDT/IDT уже настроены в boot.c, но проверяем инт3 как linux-like selftest
    klog(KLOG_INFO, "Testing IDT: int3 breakpoint...");
    __asm__ volatile("int3");
    klog(KLOG_OK, "int3 handled, IDT working");

    klog(KLOG_INFO, "Initializing syscall layer...");
    syscall_init();
    klog(KLOG_OK, "Syscall int 0x80 ready");

    const char *msg="Hello from syscall (Limine)!\n";
    klog(KLOG_INFO, "Testing syscall WRITE...");
    do_syscall(SYS_WRITE, (uint64_t)msg, strlen(msg), 1,0,0);
    klog(KLOG_OK, "syscall WRITE done");

    // GOP тесты через syscalls - рисуем внизу чтобы не перекрывать boot log (linux fb test)
    klog(KLOG_INFO, "Testing GOP via syscalls: DRAW_RECT...");
    uint32_t fb_h = gop_get_height();
    uint32_t rect_y = fb_h > 120 ? fb_h - 100 : 500;
    do_syscall(SYS_DRAW_RECT, 50, rect_y, 300,60, 0xFF0000);
    do_syscall(SYS_DRAW_RECT, 400, rect_y, 300,60, 0x00FF00);
    klog(KLOG_OK, "DRAW_RECT via syscall done (red+green at bottom)");

    // показываем что весь boot лог виден, как в linux dmesg
    klog(KLOG_INFO, "Boot log complete, dumping klog info...");
    klogf(KLOG_DEBUG, "Verbose mode: %s (debug visible)", klog_is_verbose() ? "on" : "off");
    klog(KLOG_INFO, "System ready, entering idle loop");

    // восстанавливаем курсор поверх логов
    mouse_redraw();

    klog(KLOG_OK, "Idle loop started, interrupts enabled");
    idle_forever();
}
