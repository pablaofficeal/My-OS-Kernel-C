#include "commands.h"
#include "terminal.h"
#include "shell_path.h"
#include "../editor/nano.h"
#include "../syscall.h"
#include "../userspace.h"
#include "../display.h"
#include "../system.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/mouse/usb_mouse.h"
#include "../../drivers/storage/storage_types.h"
#include "../../drivers/usb/xhci.h"
#include "../../drivers/keyboard.h"
#include "../../fs/fs_types.h"
#include "../../kernel/klog.h"
#include "../../kernel/syscall.h"
#include "../../lib/string.h"
#include "../games/snake.h"
#include "../installer/installer.h"
#include "../monitor/monitor.h"
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

static char savelog_buf[4096];
static uint32_t savelog_pos;
static uint64_t savelog_total;
static bool savelog_failed;
static void savelog_cb(char c){
    if(savelog_failed) return;
    savelog_buf[savelog_pos++]=c;
    if(savelog_pos==sizeof(savelog_buf)){
        int64_t result=userspace_syscall(SYS_FILE_APPEND,(uint64_t)"/dmesg.txt",
                                          (uint64_t)savelog_buf,savelog_pos);
        if(result<0) savelog_failed=true;
        else savelog_total+=(uint32_t)result;
        savelog_pos=0;
    }
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
    struct cpu_monitor_info cpu;
    struct memory_monitor_info memory;
    bool info_available=system_get_cpu_info(&cpu)
        && system_get_memory_info(&memory);
    uint64_t ram_mb=info_available ? memory.available_bytes/(1024*1024) : 0;
    uint64_t framebuffer_bytes=display_get_framebuffer_size_bytes();
    uint64_t framebuffer_kib=framebuffer_bytes/1024;
    uint64_t framebuffer_mib=(framebuffer_bytes+1024*1024-1)/(1024*1024);
    terminal_write("=== System Information ===\n");
    terminal_printf("Processor:  %s\n",info_available ? cpu.name : "unknown");
    terminal_printf("Usable RAM: %lu MB\n",ram_mb);
    terminal_printf("Graphics:   %s\n",display_get_protocol_name());
    terminal_printf("Video mode: %ux%u, %u bpp\n",
                    display_get_width(),display_get_height(),(unsigned int)display_get_bpp());
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
    terminal_write("usbscan: (подробный дамп смотри в dmesg)\n");
    struct usb_scan_status status={0};
    int64_t count=userspace_syscall(SYS_USB_RESCAN,(uint64_t)&status,0,0);
    if(count<0){
        terminal_write("usbscan: controller scan failed\n");
        terminal_write("usbscan: hint - проверь dmesg на pci scan: нашли ли xhci контроллер? QEMU должен быть запущен с -device qemu-xhci\n");
        return;
    }
    terminal_printf("usbscan: %d USB storage device(s) ready\n",(int)count);
    terminal_printf("xhci: controllers=%u ports=%u connected=%u addressed=%u disks=%u mice=%u stage=%u\n",
                    status.xhci_controllers,status.xhci_max_ports,
                    status.xhci_connected_ports,status.xhci_addressed_devices,
                    status.xhci_disks,status.xhci_hid_mice,status.xhci_stage);
    terminal_printf("xhci-hid: interfaces=%u hubs=%u transfer-errors=%u scratchpads=%u\n",
                    status.xhci_hid_interfaces,status.xhci_hubs,
                    status.xhci_mouse_transfer_errors,status.xhci_scratchpad_count);
    terminal_printf("xhci: error=%u (%s) usbsts=0x%x\n",
                    status.xhci_error,xhci_error_name(status.xhci_error),
                    status.xhci_usb_status);
    if(status.xhci_last_port){
        terminal_printf("xhci: last-port=%u portsc=0x%x completion=%u\n",
                        status.xhci_last_port,status.xhci_portsc,
                        status.xhci_completion_code);
    }
    // Always show EHCI even if zero, для диагностики когда устройства попали на другой шине
    terminal_printf("ehci: connected=%u high-speed=%u disks=%u failures=%u stage=%u\n",
                    status.ehci_connected_ports,status.ehci_high_speed_ports,
                    status.ehci_disks,status.ehci_failures,status.ehci_stage);
    // Expand diagnostics for the reported case: 3 devices but 0 connected => почти наверняка устройства не на xHCI bus
    if(count==0){
        terminal_write("usbscan: report the xhci lines above\n");
        terminal_write("--- диагностика ---\n");
        if(status.xhci_controllers==0){
            terminal_write("DIAG: нет xHCI контроллеров! Запусти QEMU с: -device qemu-xhci\n");
            terminal_write("  полный пример: qemu-system-x86_64 -cdrom purec_limine.iso -device qemu-xhci -drive if=none,id=usb0,file=usbdisk.img,format=raw -device usb-storage,bus=xhci.0,drive=usb0\n");
        } else if(status.xhci_connected_ports==0){
            terminal_write("DIAG: xhci виден, но connected=0 -> порты не видят CCS.\n");
            terminal_write("  причины (по порядку проверки через dmesg):\n");
            terminal_write("  1) Устройства на другом USB контроллере (UHCI/OHCI/EHCI). Смотри в dmesg: 'pci usb: ... progIF -> UHCI/OHCI' - они игнорируются драйвером.\n");
            terminal_write("  2) Укажи шину явно: -device usb-storage,bus=xhci.0,drive=...\n");
            terminal_write("  3) Проверь MMIO BAR и PORTSC дамп в dmesg: все порты 0x00000000 => непройден BIOS handoff или BAR неверный.\n");
            terminal_write("  4) QEMU after VM capture: сделай еще раз usbscan после attach, либо reboot.\n");
            terminal_write("  полный dmesg: введи 'dmesg' для детального лога каждого PORTSC и xECP.\n");
        } else if(status.xhci_addressed_devices==0){
            terminal_write("DIAG: устройства подключены но не за-addressed -> смотри в dmesg Enable Slot / Address Device ошибки, completion code\n");
        } else if(status.xhci_disks==0){
            terminal_write("DIAG: за-addressed но нет BOT интерфейса/SCSI -> проверь что образ - это mass-storage (не hub/клава) и что в dmesg есть 'BOT interface' строки\n");
        }
    } else {
        terminal_printf("usbscan: OK %d disk(s) ready, проверь 'disks' для списка /dev/sdX\n",(int)count);
    }
    terminal_write("tip: после починки проверяй 'disks' и 'dmesg | tail'\n");
}

static bool fetch_device_serial(const char *device, char out_serial[STORAGE_SERIAL_CAPACITY]){
    struct storage_device_info devices[20];
    int64_t cnt=userspace_syscall(SYS_DISK_LIST,(uint64_t)devices,20,0);
    if(cnt<0) return false;
    for(int64_t i=0;i<cnt;i++){
        if(strcmp(devices[i].name,device)==0){
            memcpy(out_serial,devices[i].serial,STORAGE_SERIAL_CAPACITY);
            return true;
        }
    }
    return false;
}

static void prompt_read_line(const char *prompt, char *out, uint32_t cap);
static bool fetch_device_serial(const char *device, char out_serial[STORAGE_SERIAL_CAPACITY]);
static void print_mkfs_error(int64_t status, const char *device){
    if(status==FS_ERROR_CONFIRMATION){
        terminal_write("mkfs.fat32: erase confirmation does not match\n");
        terminal_write("  Use: mkfs.fat32 /dev/sdX ERASE  (serial fetched automatically)\n");
        terminal_write("  Legacy: mkfs.fat32 /dev/sdX SERIAL ERASE\n");
    } else if(status==FS_ERROR_NOT_BLANK){
        terminal_write("mkfs.fat32: disk is not blank (MBR/data found)\n");
        terminal_write("  Use 'mkfs.fat32 /dev/sdX ERASE --force' to overwrite, or zero with 'install'\n");
        terminal_write("  Details in dmesg\n");
    } else if(status==FS_ERROR_BUSY){
        terminal_write("mkfs.fat32: volume already mounted on this device; reboot or choose other disk\n");
    } else if(status==FS_ERROR_TOO_SMALL){
        terminal_write("mkfs.fat32: disk too small (<32MB) or too large for FAT32 layout\n");
    } else if(status==FS_ERROR_UNSUPPORTED){
        terminal_write("mkfs.fat32: sector size must be 512 and disk must fit 32-bit LBA\n");
        terminal_write("  Check 'disks' for sector_size=512, dmesg for details\n");
        if(device){
            char s[STORAGE_SERIAL_CAPACITY]={0};
            if(fetch_device_serial(device,s)){
                terminal_printf("  dev %s serial %s\n",device,s);
            }
            struct storage_device_info devs[20];
            int64_t c=userspace_syscall(SYS_DISK_LIST,(uint64_t)devs,20,0);
            for(int64_t i=0;i<c;i++) if(strcmp(devs[i].name,device)==0){
                terminal_printf("  sectors=%u ss=%u op=%u wr=%u\n",(uint32_t)devs[i].sector_count,devs[i].sector_size,devs[i].operational,devs[i].writable);
                break;
            }
        }
    } else if(status==FS_ERROR_READ_ONLY){
        terminal_write("mkfs.fat32: device is read-only or not operational (check dmesg, xhci errors)\n");
    } else if(status==FS_ERROR_NOT_FOUND){
        terminal_write("mkfs.fat32: device not found, check 'disks'\n");
    } else if(status<0){
        terminal_printf("mkfs.fat32: failed (error %d) see dmesg\n",(int)status);
    }
}

static void format_fat32(const char *arguments){
    char device[STORAGE_DEVICE_NAME_CAPACITY]={0};
    char arg2[STORAGE_SERIAL_CAPACITY]={0};
    char arg3[16]={0};
    char extra[16]={0};
    const char *remaining;
    const char *tail;
    split_command(arguments,device,sizeof(device),&remaining);
    // trim device
    if(!device[0]){
        terminal_write("Use (simplified): mkfs.fat32 /dev/sdX ERASE\n");
        terminal_write("     mkfs.fat32 /dev/sdX --force  (overwrite non-blank)\n");
        terminal_write("Legacy: mkfs.fat32 /dev/sdX SERIAL ERASE\n");
        terminal_write("Run 'disks' to list devices\n");
        return;
    }
    split_command(remaining,arg2,sizeof(arg2),&tail);
    split_command(tail,arg3,sizeof(arg3),&remaining);
    split_command(remaining,extra,sizeof(extra),&tail);
    if(extra[0]){
        terminal_write("mkfs.fat32: too many arguments\n");
        return;
    }
    char serial[STORAGE_SERIAL_CAPACITY]={0};
    const char *approval="ERASE";
    bool force=false;
    // detect --force in arg2 or arg3
    bool has_force=(strcmp(arg2,"--force")==0 || strcmp(arg2,"-f")==0 || strcmp(arg3,"--force")==0 || strcmp(arg3,"-f")==0);
    if(has_force) force=true;
    // case: mkfs.fat32 /dev/sdb  (only device) -> show info + hint, ask interactive
    if(!arg2[0]){
        if(!fetch_device_serial(device,serial)){
            terminal_printf("mkfs.fat32: cannot find device %s (see 'disks')\n",device);
            return;
        }
        struct storage_device_info devs[20];
        int64_t c=userspace_syscall(SYS_DISK_LIST,(uint64_t)devs,20,0);
        for(int64_t i=0;i<c;i++) if(strcmp(devs[i].name,device)==0){
            terminal_printf("Device %s: %s %u MB serial %s %s\n",devs[i].name,devs[i].model,(uint32_t)(devs[i].sector_count/2048),devs[i].serial,devs[i].operational?"operational":"not operational");
            break;
        }
        char ans[16]={0};
        prompt_read_line("Type ERASE to format (or --force to overwrite): ",ans,sizeof(ans));
        if(strcmp(ans,"ERASE")!=0 && strcmp(ans,"--force")!=0 && strcmp(ans,"-f")!=0){
            terminal_write("Aborted.\n");
            return;
        }
        if(strcmp(ans,"--force")==0 || strcmp(ans,"-f")==0) force=true;
    } else if(!arg3[0]){
        // two args: device ERASE  or device --force
        if(strcmp(arg2,"ERASE")==0 || strcmp(arg2,"--force")==0 || strcmp(arg2,"-f")==0){
            if(!fetch_device_serial(device,serial)){
                terminal_printf("mkfs.fat32: cannot fetch serial for %s\n",device);
                return;
            }
            if(strcmp(arg2,"--force")==0 || strcmp(arg2,"-f")==0) force=true;
        } else {
            terminal_write("mkfs.fat32: expected ERASE. Use: mkfs.fat32 /dev/sdX ERASE\n");
            return;
        }
    } else {
        // three args: legacy device serial ERASE (with optional --force as extra? already handled)
        // if arg3 is ERASE and arg2 looks like serial
        if(strcmp(arg3,"ERASE")==0){
            memcpy(serial,arg2,sizeof(serial));
            // force already detected via extra? but legacy no force
        } else if((strcmp(arg2,"ERASE")==0 && (strcmp(arg3,"--force")==0 || strcmp(arg3,"-f")==0))){
            if(!fetch_device_serial(device,serial)){
                terminal_printf("mkfs.fat32: cannot fetch serial for %s\n",device);
                return;
            }
            force=true;
        } else {
            terminal_write("Use: mkfs.fat32 /dev/sdX ERASE  or  mkfs.fat32 /dev/sdX SERIAL ERASE\n");
            return;
        }
    }

    if(force){
        terminal_printf("Formatting %s as PURECOS FAT32 (force, serial %s); do not power off...\n",device,serial);
        // call force path via SYS_FAT32_FORMAT with serial="FORCE"? use new helper via direct force file?
        // We use normal syscall but if it returns NOT_BLANK we retry with force via second syscall number.
        // For now, try normal; if NOT_BLANK, fallback to force syscall 213 if available, else try zeroing.
        int64_t status=userspace_syscall(SYS_FAT32_FORMAT,(uint64_t)device,(uint64_t)serial,(uint64_t)approval);
        if(status==FS_ERROR_NOT_BLANK){
            terminal_write("Retrying with force (skip blank check)...\n");
            // SYS_FAT32_FORMAT_FORCE = 213 (see kernel/syscall.h). Fallback: use write zero then retry
            // try force syscall
            int64_t fstatus=userspace_syscall(213,(uint64_t)device,(uint64_t)serial,0);
            if(fstatus==0){
                terminal_printf("mkfs.fat32: force formatted PURECOS on %s\n",device);
                return;
            }
            // if force not implemented, report
            status=fstatus;
        }
        if(status==0){
            terminal_printf("mkfs.fat32: created and mounted PURECOS on %s\n",device);
        } else {
            print_mkfs_error(status,device);
        }
        return;
    }

    terminal_printf("Formatting %s as PURECOS FAT32 (serial %s); do not power off...\n",device,serial);
    int64_t status=userspace_syscall(SYS_FAT32_FORMAT,(uint64_t)device,
                                     (uint64_t)serial,(uint64_t)approval);
    if(status==0){
        terminal_printf("mkfs.fat32: created and mounted PURECOS on %s\n",device);
    } else {
        print_mkfs_error(status,device);
    }
}

static void prompt_read_line(const char *prompt, char *out, uint32_t cap){
    terminal_write(prompt);
    uint32_t len=0;
    memset(out,0,cap);
    for(;;){
        char c=keyboard_getc();
        if(c=='\r' || c=='\n'){
            terminal_putc('\n');
            break;
        }
        if(c=='\b' || c==127){
            if(len){
                len--;
                terminal_putc('\b');
            }
            continue;
        }
        if(c<' ' || c>'~') continue;
        if(len+1<cap){
            out[len++]=c;
            terminal_putc(c);
        }
    }
    out[len]=0;
}

static bool installer_write_file(const char *path, const char *content){
    int64_t r=userspace_syscall(SYS_FILE_WRITE,(uint64_t)path,(uint64_t)content,strlen(content));
    return r>=0;
}
static bool installer_create_dir(const char *path){
    int64_t r=userspace_syscall(SYS_DIR_CREATE,(uint64_t)path,0,0);
    return r==0 || r==FS_ERROR_EXISTS;
}

static void run_installer(const char *args){
    installer_run(args);
    return;
    terminal_write("\n=== PureC Installer 0.1 (archinstall-like) ===\n");
    static struct storage_device_info devs[20];
    int64_t cnt=userspace_syscall(SYS_DISK_LIST,(uint64_t)devs,20,0);
    if(cnt<=0){
        terminal_write("No disks found. Run 'disks' to diagnose.\n");
        return;
    }
    terminal_write("Available disks:\n");
    for(int64_t i=0;i<cnt;i++){
        uint32_t mb=(uint32_t)(devs[i].sector_count/2048);
        terminal_printf("  [%d] %s  %u MB  %s  serial=%s  %s  %s\n",
                        (int)i,devs[i].name,mb,devs[i].model,devs[i].serial,
                        devs[i].operational?"op":"offline",
                        devs[i].writable?"rw":"ro");
        const char *tr=devs[i].transport==STORAGE_TRANSPORT_USB_MSC?"USB-xHCI":
                       devs[i].transport==STORAGE_TRANSPORT_USB_EHCI?"USB-EHCI":
                       devs[i].transport==STORAGE_TRANSPORT_AHCI?"AHCI":"ATA";
        terminal_printf("       transport=%s ctrl=%u port=%u\n",tr,devs[i].controller,devs[i].port);
    }
    char arg_dev[STORAGE_DEVICE_NAME_CAPACITY]={0};
    const char *rem;
    split_command(args,arg_dev,sizeof(arg_dev),&rem);
    char devname[STORAGE_DEVICE_NAME_CAPACITY]={0};
    if(arg_dev[0]){
        bool ok=false;
        for(int64_t i=0;i<cnt;i++) if(strcmp(devs[i].name,arg_dev)==0) ok=true;
        if(!ok){
            terminal_printf("Device %s not found\n",arg_dev);
            return;
        }
        strcpy(devname,arg_dev);
        terminal_printf("Selected %s from argument\n",devname);
    } else {
        char sel[16]={0};
        prompt_read_line("Select disk number [0]: ",sel,sizeof(sel));
        if(!sel[0]) strcpy(sel,"0");
        int idx=sel[0]-'0';
        // allow multi-digit
        idx=0;
        for(uint32_t i=0;sel[i]>='0'&&sel[i]<='9';i++) idx=idx*10+(sel[i]-'0');
        if(idx<0 || idx>=cnt){
            terminal_write("Invalid selection\n");
            return;
        }
        strcpy(devname,devs[idx].name);
    }
    char serial[STORAGE_SERIAL_CAPACITY]={0};
    char model[STORAGE_MODEL_CAPACITY]={0};
    uint64_t sectors=0;
    for(int64_t i=0;i<cnt;i++) if(strcmp(devs[i].name,devname)==0){
        strcpy(serial,devs[i].serial);
        strcpy(model,devs[i].model);
        sectors=devs[i].sector_count;
        if(!devs[i].writable){
            terminal_printf("Device %s is read-only, cannot install\n",devname);
            return;
        }
        if(!devs[i].operational){
            terminal_printf("Device %s not operational (check dmesg)\n",devname);
            return;
        }
        break;
    }
    terminal_printf("Target: %s  %s  %u MB  serial=%s\n",devname,model,(uint32_t)(sectors/2048),serial);
    char is_uefi_ans[8]={0};
    prompt_read_line("UEFI system? [Y/n]: ",is_uefi_ans,sizeof(is_uefi_ans));
    bool is_uefi = !(is_uefi_ans[0]=='n' || is_uefi_ans[0]=='N');
    terminal_printf("Mode: %s\n",is_uefi?"UEFI (GPT+ESP)":"BIOS (MBR)");
    char hostname[32]={0};
    prompt_read_line("Hostname [purec-os]: ",hostname,sizeof(hostname));
    if(!hostname[0]) strcpy(hostname,"purec-os");
    char username[32]={0};
    prompt_read_line("Username [purec]: ",username,sizeof(username));
    if(!username[0]) strcpy(username,"purec");
    terminal_write("\nSummary:\n");
    terminal_printf("  Device   : %s\n  Hostname : %s\n  User     : %s\n  Mode     : %s\n",devname,hostname,username,is_uefi?"UEFI":"BIOS");
    terminal_write("This will ERASE all data on the target disk!\n");
    char confirm[16]={0};
    prompt_read_line("Type YES to continue: ",confirm,sizeof(confirm));
    bool is_yes = (strcmp(confirm,"YES")==0 || strcmp(confirm,"yes")==0 || strcmp(confirm,"Yes")==0 || strcmp(confirm,"YES\n")==0 || strcmp(confirm,"y")==0 || strcmp(confirm,"Y")==0);
    if(!is_yes){
        terminal_write("Aborted (need YES/yes).\n");
        return;
    }
    int64_t fmt;
    if(is_uefi){
        terminal_printf("Formatting %s as UEFI ESP (FAT32 + EFI)...\n",devname);
        fmt=userspace_syscall(214,(uint64_t)devname,(uint64_t)serial,0);
    } else {
        terminal_printf("Formatting %s as PURECOS FAT32...\n",devname);
        fmt=userspace_syscall(SYS_FAT32_FORMAT,(uint64_t)devname,(uint64_t)serial,(uint64_t)"ERASE");
        if(fmt==FS_ERROR_NOT_BLANK || fmt==FS_ERROR_BUSY){
            if(fmt==FS_ERROR_BUSY) terminal_write("Volume busy (mounted), forcing...\n");
            else terminal_write("Disk not blank, retrying with --force...\n");
            fmt=userspace_syscall(213,(uint64_t)devname,(uint64_t)serial,0);
        }
    }
    if(fmt<0){
        print_mkfs_error(fmt,devname);
        terminal_write("Install failed at format\n");
        terminal_write("Hint: run 'disks' to see mounted device, or reboot and run install on empty disk\n");
        return;
    }
    terminal_write("Creating filesystem structure...\n");
    installer_create_dir("/boot");
    installer_create_dir("/etc");
    installer_create_dir("/home");
    installer_create_dir("/purec");
    static char cfg[1024];
    // /purec/install.cfg
    memset(cfg,0,sizeof(cfg));
    strcpy(cfg,"# PureC OS Install Config\n");
    strcat(cfg,"hostname=");
    strcat(cfg,hostname);
    strcat(cfg,"\ndevice=");
    strcat(cfg,devname);
    strcat(cfg,"\nserial=");
    strcat(cfg,serial);
    strcat(cfg,"\nuser=");
    strcat(cfg,username);
    strcat(cfg,"\nversion=0.1.0\ninstalled=1\n");
    if(!installer_write_file("/purec/install.cfg",cfg)){
        terminal_write("Warning: failed to write /purec/install.cfg\n");
    }
    memset(cfg,0,sizeof(cfg));
    strcpy(cfg,hostname);
    strcat(cfg,"\n");
    installer_write_file("/etc/hostname",cfg);
    memset(cfg,0,sizeof(cfg));
    strcpy(cfg,"timeout=3\ndefault=purec\n");
    strcat(cfg,"hostname=");
    strcat(cfg,hostname);
    strcat(cfg,"\n");
    installer_write_file("/boot/loader.cfg",cfg);
    char home_path[64]={0};
    strcpy(home_path,"/home/");
    strcat(home_path,username);
    installer_create_dir(home_path);
    char readme_path[80]={0};
    strcpy(readme_path,home_path);
    strcat(readme_path,"/README");
    memset(cfg,0,sizeof(cfg));
    strcpy(cfg,"Welcome ");
    strcat(cfg,username);
    strcat(cfg, "!\nPureC OS installed.\nHostname: ");
    strcat(cfg,hostname);
    strcat(cfg,"\nDevice: ");
    strcat(cfg,devname);
    strcat(cfg,"\n");
    installer_write_file(readme_path,cfg);
    installer_write_file("/README","PureC OS - see /purec/install.cfg\n");
    terminal_write("\nInstall complete!\n");
    terminal_printf("  Config: /purec/install.cfg  Host: %s  User: %s\n",hostname,username);
    terminal_write("  Files: /etc/hostname /boot/loader.cfg\n");
    terminal_write("Run 'ls /purec' and 'cat /purec/install.cfg' to verify.\n");
    terminal_write("Reboot to test auto-mount.\n");
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
    terminal_write("  mkfs.fat32 [DEV [ERASE]]  format disk (simplified, serial auto)\n");
    terminal_write("  mkfs.fat32 DEV SERIAL ERASE (legacy)\n");
    terminal_write("  install [DEV]     interactive installer (archinstall-like)\n");
    terminal_write("  setup             alias for install\n");
    terminal_write("  dmesg             show kernel boot log\n");
    terminal_write("  uname             show system information\n");
    terminal_write("  about             show userspace information\n");
    terminal_write("  systeminfo        show detailed CPU and RAM info\n");
    terminal_write("  htop              open the system monitor window\n");
    terminal_write("  font [SIZE|FACE]  change session font\n");
    terminal_write("  snake             start the Snake game\n");
    terminal_write("  mouse             show PS/2 and USB mouse state\n");
    terminal_write("  debug [on|off]    control mouse debug panel\n");
    terminal_write("  reboot            reboot via syscall (ACPI/8042)\n");
    terminal_write("  poweroff|shutdown proper shutdown via ACPI/QEMU\n");
    terminal_write("  battery           show battery status (percent/charging)\n");
    terminal_write("  halt              stop the CPU\n");
}

static void show_mouse(void){
    struct mouse_state state=mouse_get_state();
    struct mouse_debug_state debug=mouse_get_debug_state();
    struct usb_mouse_info usb=usb_mouse_get_info();
    struct xhci_probe_stats xhci;
    xhci_get_probe_stats(&xhci);
    terminal_printf("mouse: x=%d y=%d dx=%d dy=%d buttons=0x%x\n",
                    state.x,state.y,state.dx,state.dy,(unsigned int)state.buttons);
    terminal_printf("driver: initialized=%u enabled=%u irq=%u poll=%u packets=%u\n",
                    (unsigned int)debug.initialized,(unsigned int)debug.enabled,debug.irq_count,
                    debug.poll_count,debug.packet_count);
    terminal_printf("usb-hid: connected=%u port=%u id=%x:%x reports=%u\n",
                    (unsigned int)usb.connected,(unsigned int)usb.port,
                    (unsigned int)usb.vendor_id,(unsigned int)usb.product_id,
                    usb.reports);
    terminal_printf("xhci: controllers=%u connected=%u addressed=%u hid=%u mice=%u\n",
                    xhci.controllers,xhci.connected_ports,xhci.addressed_devices,
                    xhci.hid_interfaces,xhci.hid_mice);
    terminal_printf("xhci: hubs=%u transfer-errors=%u last-error=%u completion=%u\n",
                    xhci.hubs,xhci.mouse_transfer_errors,
                    xhci.last_error,xhci.last_completion_code);
}

static void reboot_system(void){
    terminal_write("Rebooting via kernel syscall...\n");
    int64_t r=userspace_syscall(SYS_REBOOT,0,0,0);
    if(r<0){
        terminal_printf("syscall reboot failed (%d), fallback to 8042\n",(int)r);
        __asm__ volatile("cli");
        for(uint32_t i=0;i<100000;i++){
            if(!(inb(0x64)&0x02)){
                outb(0x64,0xFE);
                break;
            }
        }
        for(;;) __asm__ volatile("hlt");
    }
}

static void shutdown_system(void){
    terminal_write("Shutting down via kernel syscall...\n");
    int64_t r=userspace_syscall(SYS_SHUTDOWN,0,0,0);
    if(r<0){
        terminal_printf("syscall shutdown failed (%d), fallback halt\n",(int)r);
        for(;;) __asm__ volatile("cli; hlt");
    }
}

static void show_battery(void){
    struct battery_info info;
    memset(&info,0,sizeof(info));
    int64_t r=userspace_syscall(SYS_BATTERY_INFO,(uint64_t)&info,0,0);
    if(r<0){
        terminal_write("battery: syscall failed\n");
        return;
    }
    if(!info.present){
        terminal_write("battery: no battery present (AC only)\n");
        return;
    }
    terminal_printf("battery: %s %u%% (%s)\n", info.name, info.percent, info.status_text);
    terminal_printf("  charging: %s\n", info.charging ? "yes" : "no");
    terminal_printf("  voltage: %u mV  current: %d mA\n", info.voltage_mv, (int)info.current_ma);
    terminal_printf("  remaining: %u minutes\n", info.remaining_minutes);
    if(info.charging) terminal_write("  AC adapter: connected, battery charging\n");
    else terminal_write("  AC adapter: on battery\n");
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
    } else if(strcmp(command,"ls")==0 || strcmp(command,"LS")==0 || strcmp(command,"Ls")==0){
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
    } else if(strcmp(command,"install")==0 || strcmp(command,"setup")==0){
        run_installer(arguments);
    } else if(strcmp(command,"dmesg")==0){
        terminal_write("--- kernel log ---\n");
        klog_dump_with(terminal_putc);
        terminal_write("\n--- end kernel log ---\n");
        terminal_write("Full streaming log: /kernel.log (up to 4 GiB)\n");
        terminal_write("Hint: run 'savelog' only for an 8 MiB /dmesg.txt snapshot\n");
    } else if(strcmp(command,"savelog")==0){
        terminal_write("Saving current 8 MiB ring snapshot to /dmesg.txt...\n");
        savelog_pos=0;
        savelog_total=0;
        savelog_failed=false;
        int64_t clear_result=userspace_syscall(SYS_FILE_WRITE,(uint64_t)"/dmesg.txt",0,0);
        if(clear_result<0){
            terminal_printf("Failed to create /dmesg.txt (%d)\n",(int)clear_result);
            return;
        }
        klog_foreach(savelog_cb);
        if(!savelog_failed && savelog_pos){
            int64_t result=userspace_syscall(SYS_FILE_APPEND,(uint64_t)"/dmesg.txt",
                                              (uint64_t)savelog_buf,savelog_pos);
            if(result<0) savelog_failed=true;
            else savelog_total+=(uint32_t)result;
        }
        if(!savelog_failed) terminal_printf("Saved %lu bytes to /dmesg.txt (streamed)\n",savelog_total);
        else terminal_write("Log save stopped: disk append failed\n");
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
    } else if(strcmp(command,"htop")==0){
        monitor_run();
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
    } else if(strcmp(command,"poweroff")==0 || strcmp(command,"shutdown")==0){
        shutdown_system();
    } else if(strcmp(command,"battery")==0){
        show_battery();
    } else if(strcmp(command,"halt")==0){
        terminal_write("System halted.\n");
        for(;;) __asm__ volatile("cli; hlt");
    } else {
        terminal_printf("%s: command not found\n",command);
        terminal_write("Type 'help' to list commands.\n");
    }
}
