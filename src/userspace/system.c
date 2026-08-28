#include "system.h"
#include "syscall.h"

static struct cpu_monitor_info cached_cpu;
static bool cached_cpu_valid;

bool system_get_cpu_info(struct cpu_monitor_info *info){
    if(!info) return false;
    if(userspace_syscall(SYS_CPU_INFO,(uint64_t)info,0,0)<0) return false;
    cached_cpu=*info;
    cached_cpu_valid=true;
    return true;
}

bool system_get_memory_info(struct memory_monitor_info *info){
    if(!info) return false;
    return userspace_syscall(SYS_MEMORY_INFO,(uint64_t)info,0,0)>=0;
}

uint64_t system_uptime_ms(void){
    struct cpu_monitor_info info;
    if(system_get_cpu_info(&info)) return info.uptime_ms;
    return cached_cpu_valid ? cached_cpu.uptime_ms : 0;
}

uint64_t system_tsc_frequency_hz(void){
    struct cpu_monitor_info info;
    if(system_get_cpu_info(&info)) return info.frequency_hz;
    return cached_cpu_valid ? cached_cpu.frequency_hz : 0;
}
