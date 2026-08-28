#include "init.h"
#include "boot_diag.h"
#include "klog.h"
#include "panic.h"
#include "scheduler.h"
#include "../drivers/serial.h"
#include "../userspace/userspace.h"

static void boot_log_pause(void){
    volatile uint64_t dummy=0;
    for(uint64_t i=0;i<30000000ULL;i++){
        __asm__ volatile("pause");
        dummy+=i;
    }
    (void)dummy;
}

void init_process_start(uint32_t detected_cpu_count){
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "about to start init process");
    klog(KLOG_OK, "Booting init process...");
    boot_log_pause();

    serial_write_string("[INIT] linked userspace init\n");
    userspace_init();
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_RUN,
                         "init complete, starting scheduler");

    scheduler_init();
    int core_count=scheduler_get_core_count();
    if(detected_cpu_count>1){
        klogf(KLOG_WARN,
              "sched: Limine detected %u CPUs; 1 CPU active until AP scheduler support is installed",
              detected_cpu_count);
    }
    klogf(KLOG_INFO, "sched: active cores=%d, creating init threads",
          core_count);

    scheduler_create_thread(userspace_input_thread, 0, "init-input", 0, 0);
    scheduler_create_thread(userspace_terminal_thread, 0, "init-terminal", 1, 0);
    klog(KLOG_OK, "sched: init threads created, starting scheduler");
    serial_write_string("[SCHED] start\n");
    scheduler_start();

    serial_write_string("[INIT] fallback run\n");
    userspace_run();
    kernel_panic("init process returned unexpectedly");
}
