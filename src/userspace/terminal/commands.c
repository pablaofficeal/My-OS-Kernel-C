#include "commands.h"
#include "terminal.h"
#include "shell_path.h"
#include "../editor/nano.h"
#include "../syscall.h"
#include "../userspace.h"
#include "../../drivers/gop.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/storage/storage_types.h"
#include "../../drivers/usb/xhci.h"
#include "../../fs/fat32.h"
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

    char resolved[SHELL_PATH_CAPACITY];
    if(!shell_path_resolve(path,resolved)){
        terminal_write("cat: invalid or too long path\n");
        return;
    }

    int64_t descriptor=userspace_syscall(SYS_FILE_OPEN,(uint64_t)resolved,0,0);
    if(descriptor<0){
        terminal_printf("cat: cannot open file (error %d)\n",(int)descriptor);
        return;
    }

    char buffer[256];
    bool wrote_data=false;
    char last_character='\0';
    for(;;){
        int64_t count=userspace_syscall(SYS_FILE_READ,(uint64_t)descriptor,
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
    char target[SHELL_PATH_CAPACITY];
    if(!shell_path_resolve(path[0] ? path : ".",target)){
        terminal_write("ls: invalid or too long path\n");
        return;
    }
    struct fs_directory_entry entries[32];
    int64_t count=userspace_syscall(SYS_DIR_LIST,(uint64_t)target,
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
    char resolved[SHELL_PATH_CAPACITY];
    if(!shell_path_resolve(path,resolved)){
        terminal_printf("%s: invalid or too long path\n",command);
        return;
    }
    int64_t status=userspace_syscall(syscall_number,(uint64_t)resolved,0,0);
    if(status<0){
        terminal_printf("%s: cannot create path (error %d)\n",command,(int)status);
        return;
    }
    terminal_printf("%s: created %s\n",command,resolved);
}

static void change_directory(const char *path){
    char resolved[SHELL_PATH_CAPACITY];
    if(!shell_path_resolve(path[0] ? path : "/",resolved)){
        terminal_write("cd: invalid or too long path\n");
        return;
    }
    struct fs_directory_entry probe;
    int64_t status=userspace_syscall(SYS_DIR_LIST,(uint64_t)resolved,
                                     (uint64_t)&probe,1);
    if(status<0){
        terminal_printf("cd: cannot enter directory (error %d)\n",(int)status);
        return;
    }
    if(!shell_path_change(resolved)){
        terminal_write("cd: failed to update current directory\n");
    }
}

static void open_editor(const char *path){
    if(!path[0]){
        terminal_write("nano: missing file path\n");
        return;
    }
    char resolved[SHELL_PATH_CAPACITY];
    if(!shell_path_resolve(path,resolved)){
        terminal_write("nano: invalid or too long path\n");
        return;
    }
    nano_open(resolved);
}

static void show_disks(void){
    (void)userspace_syscall(SYS_USB_RESCAN,0,0,0);
    struct storage_device_info devices[20];
    struct storage_controller_info controllers[8];
    int64_t disk_count=userspace_syscall(SYS_DISK_LIST,(uint64_t)devices,20,0);
    int64_t controller_count=userspace_syscall(SYS_STORAGE_CONTROLLERS,
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
            } else if(devices[index].transport==STORAGE_TRANSPORT_ATA_PIO){
                const char *channel=devices[index].channel ? "secondary" : "primary";
                const char *drive=devices[index].drive ? "slave" : "master";
                terminal_printf("  bus: ATA %s %s\n",channel,drive);
            } else {
                const char *usb_host=devices[index].transport==STORAGE_TRANSPORT_USB_EHCI
                    ? "EHCI" : "xHCI";
                terminal_printf("  bus: USB %s controller %u port %u\n",usb_host,
                                (unsigned int)devices[index].controller,
                                (unsigned int)devices[index].port);
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
        terminal_write("Storage and USB controllers:\n");
        for(int64_t index=0;index<controller_count;index++){
            const char *type=controllers[index].type==STORAGE_CONTROLLER_AHCI
                ? "AHCI" : (controllers[index].type==STORAGE_CONTROLLER_NVME
                    ? "NVMe" : (controllers[index].type==STORAGE_CONTROLLER_XHCI
                        ? "xHCI" : "EHCI"));
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
            if(controllers[index].type==STORAGE_CONTROLLER_AHCI){
                terminal_write("  block I/O: DMA read/write\n");
            } else if(controllers[index].type==STORAGE_CONTROLLER_XHCI
                      || controllers[index].type==STORAGE_CONTROLLER_EHCI){
                terminal_write("  block I/O: USB Mass Storage polling\n");
            } else {
                terminal_write("  block I/O: driver not initialized\n");
            }
        }
    }
}

static const char *xhci_error_name(uint32_t error){
    switch(error){
        case XHCI_PROBE_OK: return "none";
        case XHCI_PROBE_MMIO: return "MMIO mapping failed";
        case XHCI_PROBE_CAPABILITY: return "invalid capability registers";
        case XHCI_PROBE_BIOS_HANDOFF: return "BIOS ownership timeout";
        case XHCI_PROBE_HALT_TIMEOUT: return "controller halt timeout";
        case XHCI_PROBE_RESET_TIMEOUT: return "controller reset timeout";
        case XHCI_PROBE_NOT_READY_TIMEOUT: return "controller-not-ready timeout";
        case XHCI_PROBE_PAGE_SIZE: return "4 KiB pages unsupported";
        case XHCI_PROBE_DMA_ADDRESS: return "DMA address unsupported";
        case XHCI_PROBE_SCRATCHPADS: return "too many scratchpad buffers";
        case XHCI_PROBE_RUN_TIMEOUT: return "controller run timeout";
        case XHCI_PROBE_NO_CONNECTED_PORT: return "no connected root port";
        case XHCI_PROBE_PORT_RESET: return "root-port reset failed";
        case XHCI_PROBE_EVENT_TIMEOUT: return "event ring timeout";
        case XHCI_PROBE_COMPLETION: return "xHCI completion error";
        case XHCI_PROBE_ENABLE_SLOT: return "Enable Slot failed";
        case XHCI_PROBE_ADDRESS_DEVICE: return "Address Device failed";
        case XHCI_PROBE_DEVICE_DESCRIPTOR: return "device descriptor failed";
        case XHCI_PROBE_CONFIG_DESCRIPTOR: return "configuration descriptor failed";
        case XHCI_PROBE_MASS_STORAGE_INTERFACE: return "no USB BOT interface";
        case XHCI_PROBE_CONFIGURE_ENDPOINT: return "endpoint configuration failed";
        case XHCI_PROBE_SCSI: return "USB BOT/SCSI probe failed";
        default: return "unknown";
    }
}

static void rescan_usb(void){
    terminal_write("usbscan: rescanning xHCI/EHCI root ports...\n");
    struct usb_scan_status status={0};
    int64_t count=userspace_syscall(SYS_USB_RESCAN,(uint64_t)&status,0,0);
    if(count<0){
        terminal_write("usbscan: controller scan failed\n");
        return;
    }
    terminal_printf("usbscan: %d USB storage device(s) ready\n",(int)count);
    terminal_printf("xhci: controllers=%u ports=%u scratchpads=%u connected=%u addressed=%u disks=%u stage=%u\n",
                    status.xhci_controllers,status.xhci_max_ports,
                    status.xhci_scratchpad_count,
                    status.xhci_connected_ports,status.xhci_addressed_devices,
                    status.xhci_disks,status.xhci_stage);
    terminal_printf("xhci: error=%u (%s) usbsts=0x%x\n",
                    status.xhci_error,xhci_error_name(status.xhci_error),
                    status.xhci_usb_status);
    if(status.xhci_last_port){
        terminal_printf("xhci: last-port=%u portsc=0x%x completion=%u\n",
                        status.xhci_last_port,status.xhci_portsc,
                        status.xhci_completion_code);
    }
    if(status.ehci_connected_ports || status.ehci_failures){
        terminal_printf("ehci: connected=%u high-speed=%u disks=%u failures=%u stage=%u\n",
                        status.ehci_connected_ports,status.ehci_high_speed_ports,
                        status.ehci_disks,status.ehci_failures,status.ehci_stage);
    }
    if(count==0){
        terminal_write("usbscan: report the xhci lines above\n");
    }
}

static void format_fat32(const char *arguments){
    char device[STORAGE_DEVICE_NAME_CAPACITY];
    char serial[STORAGE_SERIAL_CAPACITY];
    char approval[6];
    char extra[2];
    const char *remaining;
    const char *tail;
    split_command(arguments,device,sizeof(device),&remaining);
    split_command(remaining,serial,sizeof(serial),&tail);
    split_command(tail,approval,sizeof(approval),&remaining);
    split_command(remaining,extra,sizeof(extra),&tail);
    if(!device[0] || !serial[0] || strcmp(approval,"ERASE")!=0 || extra[0]){
        terminal_write("Use: mkfs.fat32 <device> <exact-serial> ERASE\n");
        terminal_write("The serial is shown by the disks command.\n");
        return;
    }

    terminal_printf("Formatting %s as PURECOS FAT32; do not power off...\n",device);
    int64_t status=userspace_syscall(SYS_FAT32_FORMAT,(uint64_t)device,
                                     (uint64_t)serial,(uint64_t)approval);
    if(status==FS_ERROR_CONFIRMATION){
        terminal_write("mkfs.fat32: erase confirmation does not match\n");
    } else if(status==FS_ERROR_NOT_BLANK){
        terminal_write("mkfs.fat32: refused because the disk is not blank\n");
    } else if(status==FS_ERROR_BUSY){
        terminal_write("mkfs.fat32: refused while a FAT32 volume is mounted\n");
    } else if(status==FS_ERROR_TOO_SMALL){
        terminal_write("mkfs.fat32: disk size is not supported for FAT32\n");
    } else if(status<0){
        terminal_printf("mkfs.fat32: failed (error %d)\n",(int)status);
    } else {
        terminal_printf("mkfs.fat32: created and mounted PURECOS on %s\n",device);
    }
}

static void show_help(void){
    terminal_write("Commands:\n");
    terminal_write("  help              show this command list\n");
    terminal_write("  clear | cls       clear terminal output\n");
    terminal_write("  echo <text>       print text\n");
    terminal_write("  cat <file>        read a FAT32 file through syscalls\n");
    terminal_write("  ls [directory]    list a FAT32 directory\n");
    terminal_write("  cd [directory]    change current FAT32 directory\n");
    terminal_write("  pwd               show current directory\n");
    terminal_write("  touch <file>      create an empty FAT32 file\n");
    terminal_write("  mkdir <directory> create a FAT32 directory\n");
    terminal_write("  nano <file>       open the small text editor\n");
    terminal_write("  disks             list disks and storage controllers\n");
    terminal_write("  usbscan           rescan USB ports after VM capture\n");
    terminal_write("  mkfs.fat32 DEV SERIAL ERASE  format a blank disk\n");
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
    } else if(strcmp(command,"cd")==0){
        change_directory(arguments);
    } else if(strcmp(command,"pwd")==0){
        terminal_printf("%s\n",shell_path_current());
    } else if(strcmp(command,"touch")==0){
        create_path("touch",arguments,SYS_FILE_CREATE);
    } else if(strcmp(command,"mkdir")==0){
        create_path("mkdir",arguments,SYS_DIR_CREATE);
    } else if(strcmp(command,"nano")==0){
        open_editor(arguments);
    } else if(strcmp(command,"disks")==0){
        show_disks();
    } else if(strcmp(command,"usbscan")==0){
        rescan_usb();
    } else if(strcmp(command,"mkfs.fat32")==0){
        format_fat32(arguments);
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
