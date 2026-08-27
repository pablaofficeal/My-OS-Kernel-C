#include "monitor.h"

#include "../syscall.h"
#include "../../drivers/gop.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/storage/storage_types.h"
#include "../../kernel/syscall.h"

#include <stdbool.h>
#include <stdint.h>

#define MIB (1024ULL*1024ULL)
#define GIB (1024ULL*1024ULL*1024ULL)
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 330
#define TITLE_HEIGHT 34
#define MONITOR_DISK_LIMIT 3
#define COLOR_BORDER 0x45475A
#define COLOR_WINDOW 0x1E1E2E
#define COLOR_TITLE 0xA6E3A1
#define COLOR_TEXT 0xCDD6F4
#define COLOR_MUTED 0x9399B2
#define COLOR_CLOSE 0xF38BA8
#define COLOR_CPU 0x89B4FA
#define COLOR_RAM 0xCBA6F7

static uint32_t window_x=90;
static uint32_t window_y=70;
static uint32_t window_width=WINDOW_WIDTH;
static uint32_t window_height=WINDOW_HEIGHT;
static bool visible;
static bool dragging;
static int32_t drag_offset_x;
static int32_t drag_offset_y;
static uint64_t last_refresh_tsc;
static uint64_t refresh_interval_tsc=1500000000ULL;

static uint64_t read_tsc(void){
    uint32_t low,high;
    __asm__ volatile("rdtsc":"=a"(low),"=d"(high));
    return ((uint64_t)high<<32)|low;
}

static bool point_inside(int32_t x, int32_t y, uint32_t left, uint32_t top,
                         uint32_t width, uint32_t height){
    return x>=(int32_t)left && y>=(int32_t)top
        && x<(int32_t)(left+width) && y<(int32_t)(top+height);
}

static char *append_text(char *output, const char *text){
    while(*text) *output++=*text++;
    *output='\0';
    return output;
}

static char *append_u64(char *output, uint64_t value){
    char reverse[24];
    uint32_t length=0;
    if(value==0) reverse[length++]='0';
    while(value && length<sizeof(reverse)){
        reverse[length++]=(char)('0'+value%10);
        value/=10;
    }
    while(length) *output++=reverse[--length];
    *output='\0';
    return output;
}

static void draw_bar(uint32_t x, uint32_t y, uint32_t width,
                     uint32_t percent, uint32_t color){
    if(percent>100) percent=100;
    gop_draw_rect(x,y,width,14,0x313244);
    uint32_t filled=(width*percent)/100;
    if(filled) gop_draw_rect(x,y,filled,14,color);
}

static void draw_line(uint32_t y, const char *label, uint64_t value,
                      const char *suffix){
    char line[96];
    char *position=append_text(line,label);
    position=append_u64(position,value);
    (void)append_text(position,suffix);
    gop_draw_text_sized_at(window_x+22,y,line,COLOR_TEXT,COLOR_WINDOW,9);
}

static void draw_contents(void){
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
    uint32_t content_y=window_y+TITLE_HEIGHT+12;
    gop_draw_rect(window_x+1,window_y+TITLE_HEIGHT+1,
                  window_width-2,window_height-TITLE_HEIGHT-2,COLOR_WINDOW);
    if(!available){
        gop_draw_text_sized_at(window_x+22,content_y,"Statistics unavailable",
                               COLOR_CLOSE,COLOR_WINDOW,9);
        return;
    }
    if(cpu.frequency_hz) refresh_interval_tsc=cpu.frequency_hz/2;

    gop_draw_text_sized_at(window_x+22,content_y,cpu.name,
                           COLOR_TEXT,COLOR_WINDOW,9);
    content_y+=24;
    uint32_t bar_width=window_width>434 ? 390 : window_width-44;
    draw_bar(window_x+22,content_y,bar_width,cpu.usage_percent,COLOR_CPU);
    draw_line(content_y+20,"CPU activity: ",cpu.usage_percent,"%");
    draw_line(content_y+38,"Logical CPUs: ",cpu.logical_processors,"");
    draw_line(content_y+56,"Clock: ",cpu.frequency_hz/1000000," MHz");

    uint32_t ram_percent=memory.total_bytes
        ? (uint32_t)((memory.used_bytes*100)/memory.total_bytes) : 0;
    content_y+=84;
    draw_bar(window_x+22,content_y,bar_width,ram_percent,COLOR_RAM);
    draw_line(content_y+20,"RAM used/reserved: ",memory.used_bytes/MIB," MiB");
    draw_line(content_y+38,"RAM usable: ",memory.available_bytes/MIB," MiB");
    draw_line(content_y+56,"Uptime: ",cpu.uptime_ms/1000," seconds");

    content_y+=84;
    draw_line(content_y,"Disks online: ",disks.operational_count,"");
    draw_line(content_y+18,"Disk capacity: ",disks.total_bytes/GIB," GiB");
    if(disk_count>0){
        char line[96];
        char *position=append_text(line,"Devices: ");
        for(int64_t index=0;index<disk_count;index++){
            if(index) position=append_text(position,", ");
            position=append_text(position,disk_devices[index].name);
        }
        gop_draw_text_sized_at(window_x+22,content_y+36,line,
                               COLOR_MUTED,COLOR_WINDOW,9);
    } else {
        gop_draw_text_sized_at(window_x+22,content_y+36,"Devices: none",
                               COLOR_MUTED,COLOR_WINDOW,9);
    }
}

void monitor_window_draw(void){
    if(!visible) return;
    mouse_begin_framebuffer_update();
    gop_draw_rect(window_x+5,window_y+5,window_width,window_height,0x11111B);
    gop_draw_rect(window_x,window_y,window_width,window_height,COLOR_BORDER);
    gop_draw_rect(window_x+1,window_y+1,window_width-2,TITLE_HEIGHT,COLOR_TITLE);
    gop_draw_text_sized_at(window_x+14,window_y+11,"System Monitor",
                           COLOR_WINDOW,COLOR_TITLE,10);
    gop_draw_rect(window_x+window_width-28,window_y+8,18,18,COLOR_CLOSE);
    gop_draw_text_sized_at(window_x+window_width-24,window_y+12,"x",
                           COLOR_WINDOW,COLOR_CLOSE,10);
    draw_contents();
    mouse_end_framebuffer_update();
    last_refresh_tsc=read_tsc();
}

void monitor_run(void){
    uint32_t screen_width=gop_get_width();
    uint32_t screen_height=gop_get_height();
    window_width=screen_width>WINDOW_WIDTH+20 ? WINDOW_WIDTH : screen_width-20;
    window_height=screen_height>WINDOW_HEIGHT+40 ? WINDOW_HEIGHT : screen_height-40;
    if(window_x+window_width>screen_width) window_x=10;
    if(window_y+window_height>screen_height) window_y=30;
    visible=true;
    dragging=false;
    last_refresh_tsc=0;
}

void monitor_window_close(void){
    visible=false;
    dragging=false;
}

bool monitor_window_is_visible(void){ return visible; }

bool monitor_window_contains_point(int32_t x, int32_t y){
    return visible && point_inside(x,y,window_x,window_y,
                                   window_width,window_height);
}

bool monitor_window_handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                                 bool pressed, bool released,
                                 uint32_t screen_width,
                                 uint32_t screen_height){
    if(!visible) return false;
    if(pressed && point_inside(x,y,window_x+window_width-28,window_y+8,18,18)){
        monitor_window_close();
        return true;
    }
    if(pressed && point_inside(x,y,window_x,window_y,
                               window_width,TITLE_HEIGHT)){
        dragging=true;
        drag_offset_x=x-(int32_t)window_x;
        drag_offset_y=y-(int32_t)window_y;
    }
    if(dragging && (buttons&1)){
        int32_t next_x=x-drag_offset_x;
        int32_t next_y=y-drag_offset_y;
        int32_t max_x=(int32_t)screen_width-(int32_t)window_width-6;
        int32_t max_y=(int32_t)screen_height-(int32_t)window_height-6;
        if(next_x<0) next_x=0;
        if(next_y<28) next_y=28;
        if(next_x>max_x) next_x=max_x;
        if(next_y>max_y) next_y=max_y;
        window_x=(uint32_t)next_x;
        window_y=(uint32_t)next_y;
    }
    if(released && dragging){
        dragging=false;
        return true;
    }
    return false;
}

void monitor_window_update(void){
    if(visible && !dragging
       && read_tsc()-last_refresh_tsc>=refresh_interval_tsc){
        monitor_window_draw();
    }
}
