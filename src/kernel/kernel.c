#include "kernel.h"
#include "../drivers/gop.h"
#include "../drivers/serial.h"
#include "../kernel/syscall.h"
#include "../kernel/klog.h"
#include "../kernel/system_info.h"
#include "../kernel/boot_diag.h"
#include "../kernel/panic.h"
#include "../arch/x86_64/mmio.h"
#include "../userspace/userspace.h"
#include "../lib/string.h"
#include "scheduler.h"
extern struct limine_memmap_response *memmap_response_ptr;
extern struct limine_smp_response *smp_response_ptr;
#include <stdint.h>

static inline int64_t do_syscall(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5){
    int64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "r10","r8","memory");
    return ret;
}

void kernel_main(struct limine_framebuffer *fb) {
    (void)fb;
    klog(KLOG_INFO, "Entering kernel_main...");
    klog(KLOG_INFO, "Kernel: 64-bit long mode, GOP active");

    boot_diag_checkpoint(BOOT_STAGE_SYSTEM_INFO, "probing CPU and memory map");
    system_info_init(memmap_response_ptr);
    if(system_info_usable_ram_bytes()==0)
        kernel_panic("memory map contains no usable RAM");
    klogf(KLOG_OK, "CPU detected: %s", system_info_cpu_name());
    klogf(KLOG_OK, "Usable RAM: %lu MB", system_info_usable_ram_bytes() / (1024 * 1024));
    klogf(KLOG_INFO, "Framebuffer: %dx%d", gop_get_width(), gop_get_height());

    // GDT/IDT уже настроены в boot.c, но проверяем инт3 как linux-like selftest
    klog(KLOG_INFO, "Testing IDT: int3 breakpoint...");
    __asm__ volatile("int3");
    klog(KLOG_OK, "int3 handled, IDT working");

    boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "initializing syscall layer");
    klog(KLOG_INFO, "Initializing PCI MMIO mapper...");
    mmio_init();
    if(mmio_is_ready()){
        klog(KLOG_OK, "PCI MMIO mapper ready (uncached 4 KiB mappings)");
    } else {
        klog(KLOG_WARN, "PCI MMIO mapper unavailable; AHCI/xHCI/EHCI disabled");
    }
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
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "about to call userspace_init");
    klog(KLOG_OK, "Booting userspace initialization...");

    // пауза чтобы увидеть boot log как в Linux (1 сек)
    { volatile uint64_t dummy=0; for(uint64_t i=0;i<30000000ULL;i++){ __asm__ volatile("pause"); dummy+=i; } (void)dummy; }
    serial_write_string("[USERSPACE] init\n");
    userspace_init();
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_RUN, "userspace_init complete, starting scheduler");

    // Scheduler: preemptive kernel threads on the active bootstrap CPU.
    scheduler_init();
    int core_count = scheduler_get_core_count();
    if(smp_response_ptr && smp_response_ptr->cpu_count>1){
        klogf(KLOG_WARN, "sched: Limine detected %u CPUs; 1 CPU active until AP scheduler support is installed",
              smp_response_ptr->cpu_count);
    }
    klogf(KLOG_INFO, "sched: active cores=%d, creating threads", core_count);

    // Создаём потоки: input (высший приоритет, affinity 0), terminal, monitor/idle
    extern void userspace_input_thread(void *arg);
    extern void userspace_terminal_thread(void *arg);
    // Input thread - высокий приоритет, чтобы мышь не фризила при чтении файла
    scheduler_create_thread(userspace_input_thread, NULL, "input", 0, 0);
    // Terminal thread - medium priority on the currently active CPU.
    scheduler_create_thread(userspace_terminal_thread, NULL, "terminal", 1, 0);
    // Дополнительный поток для фоновых задач (monitor) - низкий приоритет
    // Используем существующий userspace_run как fallback если scheduler не запущен
    klog(KLOG_OK, "sched: threads created, starting scheduler (preemptive tick 10ms)");
    serial_write_string("[SCHED] start\n");
    scheduler_start();

    // Если scheduler вернулся (не должен), fallback
    serial_write_string("[USERSPACE] run (fallback)\n");
    userspace_run();

    kernel_panic("userspace_run returned unexpectedly");
}
