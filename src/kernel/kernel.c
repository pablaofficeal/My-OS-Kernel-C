#include "kernel.h"
#include "../drivers/gop.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../kernel/syscall.h"
#include "../kernel/klog.h"
#include "../userspace/userspace.h"
#include "../lib/string.h"
// Forward declaration of memmap response from boot.c
extern struct limine_memmap_response *memmap_response_ptr;
#include "../lib/string.h"
#include <stdint.h>

char cpu_brand_string[49];
uint64_t total_ram_bytes = 0;

static inline int64_t do_syscall(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5){
    int64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "r10","r8","memory");
    return ret;
}

static void get_cpu_info() {
    uint32_t ebx, ecx, edx;
    uint32_t *ptr = (uint32_t*)cpu_brand_string;
    __asm__ volatile (
        "cpuid"
        : "=a"(*ptr), "=b"(ebx), "=c"(*(ptr+1)), "=d"(*(ptr+2))
        : "a"(0x80000002)
    );
    __asm__ volatile (
        "cpuid"
        : "=a"(*(ptr+3)), "=b"(*(ptr+4)), "=c"(*(ptr+5)), "=d"(*(ptr+6))
        : "a"(0x80000003)
    );
    __asm__ volatile (
        "cpuid"
        : "=a"(*(ptr+9)), "=b"(*(ptr+10)), "=c"(*(ptr+11)), "=d"(*(ptr+12))
        : "a"(0x80000004)
    );
    cpu_brand_string[48] = '\0';
}

static void calculate_total_ram() {
    if (!memmap_response_ptr) return;
    total_ram_bytes = 0;
    for (uint64_t i = 0; i < memmap_response_ptr->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap_response_ptr->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_ram_bytes += entry->length;
        }
    }
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

    get_cpu_info();
    calculate_total_ram();
    klogf(KLOG_OK, "CPU detected: %s", cpu_brand_string);
    klogf(KLOG_OK, "Total RAM: %lu MB", total_ram_bytes / (1024 * 1024));
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

    // GOP тест откладываем в userspace чтобы не перекрывать boot log
    klog(KLOG_INFO, "GOP test deferred to userspace");

    klog(KLOG_INFO, "Boot log complete");
    klogf(KLOG_DEBUG, "Verbose mode: %s (debug visible)", klog_is_verbose() ? "on" : "off");
    klog(KLOG_OK, "Booting userspace...");

    // пауза чтобы увидеть boot log как в Linux (1 сек)
    { volatile uint64_t dummy=0; for(uint64_t i=0;i<30000000ULL;i++){ __asm__ volatile("pause"); dummy+=i; } (void)dummy; }
    serial_write_string("[KERNEL] hiding boot screen, entering userspace\n");
    // скрываем boot log и переключаемся в userspace (как в Linux: boot splash -> login)
    klog_set_screen_enabled(false);
    serial_write_string("[USERSPACE] init\n");
    userspace_init();
    serial_write_string("[USERSPACE] run\n");
    userspace_run();

    // fallback если userspace вернётся
    mouse_redraw();
    klog_set_screen_enabled(true);
    klog(KLOG_WARN, "Userspace exited, fallback to idle");
    idle_forever();
}