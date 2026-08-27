#include "monitor.h"

#include "../syscall.h"
#include "../terminal/terminal.h"
#include "../../kernel/syscall.h"

#include <stdint.h>

#define MIB (1024ULL*1024ULL)
#define GIB (1024ULL*1024ULL*1024ULL)

void monitor_run(void){
    struct cpu_monitor_info cpu;
    struct memory_monitor_info memory;
    struct disk_monitor_info disks;
    if(userspace_syscall(SYS_CPU_INFO,(uint64_t)&cpu,0,0)<0
       || userspace_syscall(SYS_MEMORY_INFO,(uint64_t)&memory,0,0)<0
       || userspace_syscall(SYS_DISK_STATS,(uint64_t)&disks,0,0)<0){
        terminal_write("monitor: kernel statistics unavailable\n");
        return;
    }

    terminal_write("--- PureC Monitor ---\n");
    terminal_printf("CPU    %s\n",cpu.name);
    terminal_printf("       logical=%u clock=%lu MHz uptime=%lu s\n",
                    cpu.logical_processors,
                    (unsigned long)(cpu.frequency_hz/1000000),
                    (unsigned long)(cpu.uptime_ms/1000));
    terminal_printf("RAM    total=%lu MiB available=%lu MiB\n",
                    (unsigned long)(memory.total_bytes/MIB),
                    (unsigned long)(memory.available_bytes/MIB));
    terminal_printf("VIDEO  framebuffer=%lu MiB\n",
                    (unsigned long)((memory.framebuffer_bytes+MIB-1)/MIB));
    terminal_printf("DISK   devices=%u online=%u capacity=%lu GiB\n",
                    disks.device_count,disks.operational_count,
                    (unsigned long)(disks.total_bytes/GIB));
    terminal_write("RAM available is physical usable memory; allocator accounting is not active.\n");
}
