#include "commands.h"
#include "terminal.h"
#include "../userspace.h"
#include "../../drivers/gop.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../kernel/klog.h"
#include "../../kernel/system_info.h"
#include "../../lib/string.h"
#include "../games/snake.h"
#include <stdint.h>
#include <stdbool.h>

static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value){
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static bool is_space(char c){ return c==' ' || c=='\t'; }

static void split_command(const char *line, char *command, uint32_t capacity,
                          const char **arguments){
    while(is_space(*line)) line++;
    uint32_t length=0;
    while(*line && !is_space(*line)){
        if(length+1<capacity) command[length++]=*line;
        line++;
    }
    command[length]=0;
    while(is_space(*line)) line++;
    *arguments=line;
}

static void show_systeminfo(void){
    uint64_t ram_mb=system_info_usable_ram_bytes()/(1024*1024);
    uint64_t framebuffer_kib=gop_get_framebuffer_size_bytes()/1024;
    terminal_write("=== System Information ===\n");
    terminal_printf("Processor:  %s\n",system_info_cpu_name());
    terminal_printf("Usable RAM: %lu MB\n",ram_mb);
    terminal_printf("Graphics:   %s\n",gop_get_protocol_name());
    terminal_printf("Video mode: %ux%u, %u bpp\n",
                    gop_get_width(),gop_get_height(),(unsigned int)gop_get_bpp());
    terminal_printf("Framebuffer memory: %lu KiB (mapped)\n",framebuffer_kib);
    terminal_write("Total VRAM: not exposed by boot protocol\n");
    terminal_write("==========================\n");
}

static bool parse_font_size(const char *text, uint32_t *size){
    while(is_space(*text)) text++;
    if(*text<'0' || *text>'9') return false;

    uint32_t value=0;
    while(*text>='0' && *text<='9'){
        value=value*10+(uint32_t)(*text-'0');
        if(value>16) return false;
        text++;
    }
    while(is_space(*text)) text++;
    if(*text) return false;
    *size=value;
    return true;
}

static void configure_font(const char *arguments){
    if(!arguments[0]){
        terminal_printf("Font: %s, %u px\n",
                        terminal_get_font_face(),terminal_get_font_size());
        terminal_write("Faces: classic, thin, bold\n");
        terminal_write("Use: font <8-16|face>\n");
        return;
    }

    if(arguments[0]>='0' && arguments[0]<='9'){
        uint32_t size;
        if(!parse_font_size(arguments,&size) || !terminal_set_font_size(size)){
            terminal_write("font: size must be between 8 and 16\n");
            return;
        }
        terminal_printf("Font size changed to %u px for this session.\n",size);
        return;
    }

    if(!terminal_set_font_face(arguments)){
        terminal_write("font: available faces are classic, thin and bold\n");
        return;
    }
    terminal_printf("Font face changed to %s for this session.\n",terminal_get_font_face());
}

static void show_help(void){
    terminal_write("Commands:\n");
    terminal_write("  help              show this command list\n");
    terminal_write("  clear | cls       clear terminal output\n");
    terminal_write("  echo <text>       print text\n");
    terminal_write("  dmesg             show kernel boot log\n");
    terminal_write("  uname             show system information\n");
    terminal_write("  about             show userspace information\n");
    terminal_write("  systeminfo        show detailed CPU and RAM info\n");
    terminal_write("  font [SIZE|FACE]  change session font\n");
    terminal_write("  snake             start the Snake game\n");
    terminal_write("  mouse             show PS/2 mouse state\n");
    terminal_write("  debug [on|off]    control mouse debug panel\n");
    terminal_write("  reboot            reboot through the 8042\n");
    terminal_write("  halt              stop the CPU\n");
}

static void show_mouse(void){
    struct mouse_state state=mouse_get_state();
    struct mouse_debug_state debug=mouse_get_debug_state();
    terminal_printf("mouse: x=%d y=%d dx=%d dy=%d buttons=0x%x\n",
                    state.x,state.y,state.dx,state.dy,(unsigned int)state.buttons);
    terminal_printf("driver: initialized=%u enabled=%u irq=%u poll=%u packets=%u\n",
                    (unsigned int)debug.initialized,(unsigned int)debug.enabled,debug.irq_count,
                    debug.poll_count,debug.packet_count);
}

static void reboot_system(void){
    terminal_write("Rebooting...\n");
    __asm__ volatile("cli");
    for(uint32_t i=0;i<100000;i++){
        if(!(inb(0x64)&0x02)){
            outb(0x64,0xFE);
            break;
        }
    }
    for(;;) __asm__ volatile("hlt");
}

void commands_execute(const char *line){
    char command[32];
    const char *arguments;
    split_command(line,command,sizeof(command),&arguments);
    if(command[0]==0) return;

    if(strcmp(command,"help")==0){
        show_help();
    } else if(strcmp(command,"clear")==0 || strcmp(command,"cls")==0){
        terminal_clear();
    } else if(strcmp(command,"echo")==0){
        terminal_write(arguments);
        terminal_putc('\n');
    } else if(strcmp(command,"dmesg")==0){
        terminal_write("--- kernel log ---\n");
        klog_dump_with(terminal_putc);
        terminal_write("\n--- end kernel log ---\n");
    } else if(strcmp(command,"uname")==0){
        terminal_write("PureC OS 0.1.0 x86_64\n");
    } else if(strcmp(command,"about")==0){
        terminal_printf("PureC userspace 0.2.0, framebuffer %ux%u\n",
                        userspace_get_width(),userspace_get_height());
        terminal_printf("Terminal module: %ux%u window, %s %ux%u glyphs\n",
                        terminal_get_window_width(),terminal_get_window_height(),
                        terminal_get_font_face(),
                        terminal_get_font_size(),terminal_get_font_size());
    } else if(strcmp(command,"systeminfo")==0){
        show_systeminfo();
    } else if(strcmp(command,"font")==0){
        configure_font(arguments);
    } else if(strcmp(command,"snake")==0){
        snake_run();
    } else if(strcmp(command,"mouse")==0){
        show_mouse();
    } else if(strcmp(command,"debug")==0){
        bool enabled=mouse_get_debug_overlay();
        if(strcmp(arguments,"on")==0) enabled=true;
        else if(strcmp(arguments,"off")==0) enabled=false;
        else enabled=!enabled;
        userspace_set_mouse_debug(enabled);
        terminal_printf("Mouse debug panel: %s\n",enabled ? "on" : "off");
    } else if(strcmp(command,"reboot")==0){
        reboot_system();
    } else if(strcmp(command,"halt")==0){
        terminal_write("System halted.\n");
        for(;;) __asm__ volatile("cli; hlt");
    } else {
        terminal_printf("%s: command not found\n",command);
        terminal_write("Type 'help' to list commands.\n");
    }
}