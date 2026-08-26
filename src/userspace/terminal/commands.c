#include "commands.h"
#include "terminal.h"
#include "../userspace.h"
#include "../../drivers/gop.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/storage/storage_types.h"
#include "../../fs/fs_types.h"
#include "../../kernel/klog.h"
#include "../../kernel/syscall.h"
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

static int64_t invoke_syscall(uint64_t number, uint64_t argument1,
                              uint64_t argument2, uint64_t argument3){
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number),"b"(argument1),"c"(argument2),"d"(argument3)
        : "r10","r8","memory"
    );
    return result;
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
    uint64_t framebuffer_bytes=gop_get_framebuffer_size_bytes();
    uint64_t framebuffer_kib=framebuffer_bytes/1024;
    uint64_t framebuffer_mib=(framebuffer_bytes+1024*1024-1)/(1024*1024);
    terminal_write("=== System Information ===\n");
    terminal_printf("Processor:  %s\n",system_info_cpu_name());
    terminal_printf("Usable RAM: %lu MB\n",ram_mb);
    terminal_printf("Graphics:   %s\n",gop_get_protocol_name());
    terminal_printf("Video mode: %ux%u, %u bpp\n",
                    gop_get_width(),gop_get_height(),(unsigned int)gop_get_bpp());
    terminal_printf("Framebuffer: %lu MiB mapped (%lu KiB exact)\n",
                    framebuffer_mib,framebuffer_kib);
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
        terminal_write("Faces: classic, clean, bold\n");
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
        terminal_write("font: available faces are classic, clean and bold\n");
        return;
    }
    terminal_printf("Font face changed to %s for this session.\n",terminal_get_font_face());
}

static void show_file(const char *path){
    if(!path[0]){
        terminal_write("cat: missing file path\n");
        return;
    }

    int64_t descriptor=invoke_syscall(SYS_FILE_OPEN,(uint64_t)path,0,0);
    if(descriptor<0){
        terminal_printf("cat: cannot open file (error %d)\n",(int)descriptor);
        return;
    }

    char buffer[256];
    bool wrote_data=false;
    char last_character='\0';
    for(;;){
        int64_t count=invoke_syscall(SYS_FILE_READ,(uint64_t)descriptor,
                                     (uint64_t)buffer,sizeof(buffer));
        if(count<0){
            terminal_printf("cat: read failed (error %d)\n",(int)count);
            return;
        }
        if(count==0) break;
        for(int64_t index=0;index<count;index++){
            terminal_putc(buffer[index]);
            last_character=buffer[index];
        }
        wrote_data=true;
    }
    if(wrote_data && last_character!='\n') terminal_putc('\n');
}

static void list_directory(const char *path){
    const char *target=path[0] ? path : "/";
    struct fs_directory_entry entries[32];
    int64_t count=invoke_syscall(SYS_DIR_LIST,(uint64_t)target,
                                 (uint64_t)entries,32);
    if(count<0){
        terminal_printf("ls: cannot list directory (error %d)\n",(int)count);
        return;
    }
    if(count==0){
        terminal_write("(empty)\n");
        return;
    }

    for(int64_t index=0;index<count;index++){
        if(entries[index].attributes&FS_ATTRIBUTE_DIRECTORY){
            terminal_printf("[DIR]  %s\n",entries[index].name);
        } else {
            terminal_printf("[FILE] %s  %u bytes\n",entries[index].name,
                            entries[index].size);
        }
    }
}

static void create_path(const char *command, const char *path,
                        uint64_t syscall_number){
    if(!path[0]){
        terminal_printf("%s: missing path\n",command);
        return;
    }
    int64_t status=invoke_syscall(syscall_number,(uint64_t)path,0,0);
    if(status<0){
        terminal_printf("%s: cannot create path (error %d)\n",command,(int)status);
        return;
    }
    terminal_printf("%s: created %s\n",command,path);
}

static void show_disks(void){
    struct storage_device_info devices[12];
    struct storage_controller_info controllers[8];
    int64_t disk_count=invoke_syscall(SYS_DISK_LIST,(uint64_t)devices,12,0);
    int64_t controller_count=invoke_syscall(SYS_STORAGE_CONTROLLERS,
                                            (uint64_t)controllers,8,0);
    if(disk_count<0 || controller_count<0){
        terminal_write("disks: device enumeration failed\n");
        return;
    }
    if(disk_count==0 && controller_count==0){
        terminal_write("No supported storage hardware detected.\n");
        return;
    }

    if(disk_count>0){
        terminal_write("Block devices:\n");
        for(int64_t index=0;index<disk_count;index++){
            uint64_t size_mib=devices[index].sector_count/2048;
            terminal_printf("%s  %lu MiB  %s%s\n",devices[index].name,size_mib,
                            devices[index].operational
                                ? (devices[index].writable ? "rw" : "ro")
                                : "identify-only",
                            devices[index].selected ? "  [active]" : "");
            if(devices[index].transport==STORAGE_TRANSPORT_AHCI){
                terminal_printf("  bus: AHCI controller %u port %u\n",
                                (unsigned int)devices[index].controller,
                                (unsigned int)devices[index].port);
            } else {
                const char *channel=devices[index].channel ? "secondary" : "primary";
                const char *drive=devices[index].drive ? "slave" : "master";
                terminal_printf("  bus: ATA %s %s\n",channel,drive);
            }
            terminal_printf("  model: %s\n",
                            devices[index].model[0]
                                ? devices[index].model : "ATA disk");
            if(devices[index].serial[0]){
                terminal_printf("  serial: %s\n",devices[index].serial);
            }
        }
    }

    if(controller_count>0){
        terminal_write("PCI storage controllers (detection only):\n");
        for(int64_t index=0;index<controller_count;index++){
            const char *type=controllers[index].type==STORAGE_CONTROLLER_AHCI
                ? "AHCI" : "NVMe";
            terminal_printf("%s  %s  pci %u:%u.%u  id %x:%x\n",
                            controllers[index].name,type,
                            (unsigned int)controllers[index].bus,
                            (unsigned int)controllers[index].slot,
                            (unsigned int)controllers[index].function,
                            (unsigned int)controllers[index].vendor_id,
                            (unsigned int)controllers[index].device_id);
            if(controllers[index].register_base<=0xFFFFFFFFULL){
                terminal_printf("  registers: 0x%x\n",
                                (unsigned int)controllers[index].register_base);
            } else {
                terminal_printf("  registers high:low = 0x%x:0x%x\n",
                                (unsigned int)(controllers[index].register_base>>32),
                                (unsigned int)controllers[index].register_base);
            }
            terminal_write(controllers[index].type==STORAGE_CONTROLLER_AHCI
                ? "  block I/O: IDENTIFY only\n"
                : "  block I/O: driver not initialized\n");
        }
    }
}

static void show_help(void){
    terminal_write("Commands:\n");
    terminal_write("  help              show this command list\n");
    terminal_write("  clear | cls       clear terminal output\n");
    terminal_write("  echo <text>       print text\n");
    terminal_write("  cat <file>        read a FAT32 file through syscalls\n");
    terminal_write("  ls [directory]    list a FAT32 directory\n");
    terminal_write("  touch <file>      create an empty FAT32 file\n");
    terminal_write("  mkdir <directory> create a FAT32 directory\n");
    terminal_write("  disks             list disks and storage controllers\n");
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
    } else if(strcmp(command,"cat")==0){
        show_file(arguments);
    } else if(strcmp(command,"ls")==0){
        list_directory(arguments);
    } else if(strcmp(command,"touch")==0){
        create_path("touch",arguments,SYS_FILE_CREATE);
    } else if(strcmp(command,"mkdir")==0){
        create_path("mkdir",arguments,SYS_DIR_CREATE);
    } else if(strcmp(command,"disks")==0){
        show_disks();
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
