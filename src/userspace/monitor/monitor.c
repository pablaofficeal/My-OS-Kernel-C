#include "monitor.h"

#include "../syscall.h"
#include "../terminal/terminal.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/mouse/usb_mouse.h"
#include "../../drivers/storage/storage_types.h"
#include "../../drivers/timer.h"
#include "../../kernel/syscall.h"

#include <stdbool.h>
#include <stdint.h>

#define MIB (1024ULL*1024ULL)
#define GIB (1024ULL*1024ULL*1024ULL)
#define BAR_WIDTH 28
#define MONITOR_REFRESH_MS 500
#define INPUT_POLL_MS 10
#define MONITOR_DISK_LIMIT 4

static void make_bar(char output[BAR_WIDTH+3], uint32_t percent){
    if(percent>100) percent=100;
    uint32_t filled=(percent*BAR_WIDTH+50)/100;
    output[0]='[';
    for(uint32_t index=0;index<BAR_WIDTH;index++){
        output[index+1]=index<filled ? '#' : '-';
    }
    output[BAR_WIDTH+1]=']';
    output[BAR_WIDTH+2]='\0';
}

static bool poll_exit_key(void){
    ps2_mouse_poll();
    usb_mouse_poll();
    keyboard_poll();
    char key;
    while(keyboard_try_getc(&key)){
        if(key=='q' || key=='Q' || key==27 || key==3) return true;
    }
    return false;
}

static void draw_monitor(void){
    struct cpu_monitor_info cpu;
    struct memory_monitor_info memory;
    struct disk_monitor_info disks;
    struct storage_device_info disk_devices[MONITOR_DISK_LIMIT];
    bool available=userspace_syscall(SYS_CPU_INFO,(uint64_t)&cpu,0,0)>=0
        && userspace_syscall(SYS_MEMORY_INFO,(uint64_t)&memory,0,0)>=0
        && userspace_syscall(SYS_DISK_STATS,(uint64_t)&disks,0,0)>=0;
    int64_t disk_count=userspace_syscall(SYS_DISK_LIST,
                                         (uint64_t)disk_devices,
                                         MONITOR_DISK_LIMIT,0);

    mouse_begin_framebuffer_update();
    terminal_clear();
    terminal_write_colored("PureC Monitor",0x89B4FA);
    terminal_write("   refresh 500 ms   q/esc: exit\n\n");
    if(!available){
        terminal_write("Kernel statistics unavailable\n");
        mouse_end_framebuffer_update();
        return;
    }

    char cpu_bar[BAR_WIDTH+3];
    char ram_bar[BAR_WIDTH+3];
    uint32_t ram_percent=memory.total_bytes
        ? (uint32_t)((memory.used_bytes*100)/memory.total_bytes) : 0;
    make_bar(cpu_bar,cpu.usage_percent);
    make_bar(ram_bar,ram_percent);

    terminal_printf("CPU  %s\n",cpu.name);
    terminal_printf("     %s %u%%  %u logical  %lu MHz\n",
                    cpu_bar,cpu.usage_percent,cpu.logical_processors,
                    (unsigned long)(cpu.frequency_hz/1000000));
    terminal_printf("RAM  %s %u%%\n",ram_bar,ram_percent);
    terminal_printf("     used/reserved %lu MiB / total %lu MiB   usable %lu MiB\n",
                    (unsigned long)(memory.used_bytes/MIB),
                    (unsigned long)(memory.total_bytes/MIB),
                    (unsigned long)(memory.available_bytes/MIB));
    terminal_printf("UP   %lu s    FB %lu MiB\n",
                    (unsigned long)(cpu.uptime_ms/1000),
                    (unsigned long)((memory.framebuffer_bytes+MIB-1)/MIB));
    terminal_printf("DISK %u device(s), %u online, %lu GiB total\n",
                    disks.device_count,disks.operational_count,
                    (unsigned long)(disks.total_bytes/GIB));
    if(disk_count<=0){
        terminal_write("     no storage devices detected\n");
    } else {
        for(int64_t index=0;index<disk_count;index++){
            uint64_t bytes=disk_devices[index].sector_count
                *disk_devices[index].sector_size;
            terminal_printf("     %s  %lu MiB  %s\n",disk_devices[index].name,
                            (unsigned long)(bytes/MIB),
                            disk_devices[index].operational ? "online" : "offline");
        }
    }
    mouse_end_framebuffer_update();
}

void monitor_run(void){
    bool running=true;
    while(running){
        draw_monitor();
        for(uint32_t elapsed=0;elapsed<MONITOR_REFRESH_MS;
            elapsed+=INPUT_POLL_MS){
            if(poll_exit_key()){
                running=false;
                break;
            }
            timer_sleep(INPUT_POLL_MS);
        }
    }
    terminal_write("\nmonitor stopped\n");
}
