#include "syscall.h"
#include "klog.h"
#include "boot_diag.h"
#include "../drivers/serial.h"
#include "../drivers/gop.h"
#include "../drivers/vga.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/storage/block_device.h"
#include "../drivers/storage/ahci.h"
#include "../drivers/storage/storage_probe.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/usb/ehci.h"
#include "../drivers/timer.h"
#include "../drivers/audio.h"
#include "../fs/vfs.h"
#include "system_info.h"
#include "../lib/string.h"
#include "../drivers/power.h"
#include "../kernel/scheduler.h"
#include <stdint.h>
#include <stdbool.h>

static volatile bool filesystem_syscall_busy;

static void filesystem_syscall_lock(void){
    while(__atomic_test_and_set(&filesystem_syscall_busy,__ATOMIC_ACQUIRE))
        scheduler_yield();
}

static void filesystem_syscall_unlock(void){
    __atomic_clear(&filesystem_syscall_busy,__ATOMIC_RELEASE);
}

static void print_hex(uint64_t v){
    const char *h="0123456789ABCDEF";
    char buf[17]; buf[16]=0;
    for(int i=15;i>=0;i--){ buf[i]=h[v&0xF]; v>>=4; }
    serial_write_string("0x"); serial_write_string(buf);
}

int64_t syscall_handler(struct syscall_regs *r){
    uint64_t n = r->rax;
    uint64_t a1 = r->rbx;
    uint64_t a2 = r->rcx;
    uint64_t a3 = r->rdx;
    // uint64_t a4 = r->rsi;
    // uint64_t a5 = r->rdi;
    switch(n){
        case SYS_WRITE: {
            const char *s = (const char*)(uintptr_t)a1;
            uint64_t len = a2;
            // fd в a3 игнорируем, пишем в serial+gop
            if(!s) return -1;
            for(uint64_t i=0;i<len;i++){ serial_putc(s[i]); gop_putc(s[i]); }
            return (int64_t)len;
        }
        case SYS_CLEAR: {
            uint32_t color = (uint32_t)a1;
            gop_clear(color);
            if(!gop_is_available()) vga_clear();
            return 0;
        }
        case SYS_DRAW_RECT: {
            // a1=x, a2=y, a3=w, rsi=h, rdi=color (передаем через rsi/rdi)
            uint32_t x=(uint32_t)a1, y=(uint32_t)a2, w=(uint32_t)a3, h=(uint32_t)r->rsi;
            uint32_t c=(uint32_t)r->rdi;
            gop_draw_rect(x,y,w,h,c);
            return 0;
        }
        case SYS_DRAW_LINE: {
            uint32_t x0=(uint32_t)a1, y0=(uint32_t)a2, x1=(uint32_t)a3, y1=(uint32_t)r->rsi;
            uint32_t c=(uint32_t)r->rdi;
            gop_draw_line(x0,y0,x1,y1,c);
            return 0;
        }
        case SYS_FB_INFO: {
            struct framebuffer_info *info=(struct framebuffer_info*)(uintptr_t)a1;
            if(!info) return -1;
            memset(info,0,sizeof(*info));
            info->width=gop_get_width();
            info->height=gop_get_height();
            info->pitch=gop_get_pitch();
            info->size_bytes=gop_get_framebuffer_size_bytes();
            info->bpp=gop_get_bpp();
            info->available=gop_is_available() ? 1 : 0;
            strncpy(info->protocol_name,gop_get_protocol_name(),
                    sizeof(info->protocol_name)-1);
            return 0;
        }
        case SYS_DRAW_TEXT: {
            struct framebuffer_text_request *request=
                (struct framebuffer_text_request*)(uintptr_t)a1;
            if(!request || !request->text) return -1;
            gop_draw_text_at(request->x,request->y,request->text,
                             request->fg,request->bg);
            return 0;
        }
        case SYS_DRAW_TEXT_SIZED: {
            struct framebuffer_text_request *request=
                (struct framebuffer_text_request*)(uintptr_t)a1;
            if(!request || !request->text) return -1;
            gop_draw_text_sized_at(request->x,request->y,request->text,
                                   request->fg,request->bg,request->size);
            return 0;
        }
        case SYS_SCROLL_RECT_UP: {
            struct framebuffer_scroll_request *request=
                (struct framebuffer_scroll_request*)(uintptr_t)a1;
            if(!request) return -1;
            gop_scroll_rect_up(request->x,request->y,request->w,request->h,
                               request->amount,request->fill_color);
            return 0;
        }
        case SYS_SET_FONT_FACE:
            if(a1>GOP_FONT_BOLD) return -1;
            gop_set_font_face((enum gop_font_face)a1);
            return 0;
        case SYS_GET_FONT_FACE:
            return (int64_t)gop_get_font_face();
        case SYS_GETPID:
            return 42;
        case SYS_GET_MOUSE: {
            struct mouse_state *out = (struct mouse_state*)(uintptr_t)a1;
            if(!out) return -1;
            *out = mouse_get_state();
            return 0;
        }
        case SYS_FILE_OPEN: {
            filesystem_syscall_lock();
            int32_t result=vfs_open((const char*)(uintptr_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_READ: {
            filesystem_syscall_lock();
            int32_t result=vfs_read((int32_t)a1,(void*)(uintptr_t)a2,(uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_CLOSE: {
            filesystem_syscall_lock();
            int32_t result=vfs_close((int32_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_DELETE: {
            filesystem_syscall_lock();
            int32_t result=vfs_delete((const char*)(uintptr_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_RENAME: {
            filesystem_syscall_lock();
            int32_t result=vfs_rename((const char*)(uintptr_t)a1,
                                      (const char*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_MOVE: {
            filesystem_syscall_lock();
            int32_t result=vfs_move((const char*)(uintptr_t)a1,
                                    (const char*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_DIR_LIST: {
            filesystem_syscall_lock();
            int32_t result=vfs_list((const char*)(uintptr_t)a1,
                                    (struct fs_directory_entry*)(uintptr_t)a2,
                                    (uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_CREATE: {
            filesystem_syscall_lock();
            int32_t result=vfs_create_file((const char*)(uintptr_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_DIR_CREATE: {
            filesystem_syscall_lock();
            int32_t result=vfs_create_directory((const char*)(uintptr_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_DISK_LIST:
            return block_device_list((struct storage_device_info*)(uintptr_t)a1,
                                     (uint32_t)a2);
        case SYS_STORAGE_CONTROLLERS:
            return storage_controller_list(
                (struct storage_controller_info*)(uintptr_t)a1,(uint32_t)a2);
        case SYS_FAT32_FORMAT: {
            filesystem_syscall_lock();
            int32_t result=vfs_format_device((const char*)(uintptr_t)a1,
                                             (const char*)(uintptr_t)a2,
                                             (const char*)(uintptr_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FAT32_FORMAT_FORCE: {
            filesystem_syscall_lock();
            int32_t result=vfs_format_device_force((const char*)(uintptr_t)a1,
                                                   (const char*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FAT32_FORMAT_UEFI: {
            filesystem_syscall_lock();
            int32_t result=vfs_format_uefi_device((const char*)(uintptr_t)a1,
                                                  (const char*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_WRITE: {
            filesystem_syscall_lock();
            int32_t result=vfs_write_file((const char*)(uintptr_t)a1,
                                          (const void*)(uintptr_t)a2,
                                          (uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_USB_RESCAN: {
            uint32_t count=block_device_rescan_usb();
            struct xhci_probe_stats xhci_stats;
            struct ehci_probe_stats ehci_stats;
            xhci_get_probe_stats(&xhci_stats);
            ehci_get_probe_stats(&ehci_stats);
            struct usb_scan_status *status=(struct usb_scan_status*)(uintptr_t)a1;
            if(status){
                status->xhci_controllers=xhci_stats.controllers;
                status->xhci_connected_ports=xhci_stats.connected_ports;
                status->xhci_addressed_devices=xhci_stats.addressed_devices;
                status->xhci_disks=xhci_stats.mass_storage_devices;
                status->xhci_hid_mice=xhci_stats.hid_mice;
                status->xhci_hid_interfaces=xhci_stats.hid_interfaces;
                status->xhci_hubs=xhci_stats.hubs;
                status->xhci_mouse_transfer_errors=xhci_stats.mouse_transfer_errors;
                status->xhci_failures=xhci_stats.failures;
                status->xhci_stage=xhci_stats.last_stage;
                status->xhci_error=xhci_stats.last_error;
                status->xhci_last_port=xhci_stats.last_port;
                status->xhci_portsc=xhci_stats.last_portsc;
                status->xhci_completion_code=xhci_stats.last_completion_code;
                status->xhci_max_ports=xhci_stats.max_ports;
                status->xhci_usb_status=xhci_stats.usb_status;
                status->xhci_scratchpad_count=xhci_stats.scratchpad_count;
                status->ehci_connected_ports=ehci_stats.connected_ports;
                status->ehci_high_speed_ports=ehci_stats.high_speed_ports;
                status->ehci_disks=ehci_stats.mass_storage_devices;
                status->ehci_failures=ehci_stats.failures;
                status->ehci_stage=ehci_stats.last_stage;
            }
            klogf(KLOG_INFO,"usb-rescan: xhci connected=%u addressed=%u disks=%u failures=%u stage=%u",
                  xhci_stats.connected_ports,xhci_stats.addressed_devices,
                  xhci_stats.mass_storage_devices,xhci_stats.failures,
                  xhci_stats.last_stage);
            klogf(KLOG_INFO,"usb-rescan: xhci error=%u scratchpads=%u port=%u portsc=0x%x completion=%u usbsts=0x%x",
                  xhci_stats.last_error,xhci_stats.scratchpad_count,
                  xhci_stats.last_port,xhci_stats.last_portsc,
                  xhci_stats.last_completion_code,xhci_stats.usb_status);
            klogf(KLOG_INFO,"usb-rescan: ehci connected=%u high-speed=%u disks=%u failures=%u stage=%u",
                  ehci_stats.connected_ports,ehci_stats.high_speed_ports,
                  ehci_stats.mass_storage_devices,ehci_stats.failures,
                  ehci_stats.last_stage);
            if(count){
                filesystem_syscall_lock();
                if(!vfs_is_root_mounted()) (void)vfs_mount_root();
                filesystem_syscall_unlock();
            }
            return count;
        }
        case SYS_CPU_INFO: {
            struct cpu_monitor_info *info=(struct cpu_monitor_info*)(uintptr_t)a1;
            if(!info) return -1;
            memset(info,0,sizeof(*info));
            strncpy(info->name,system_info_cpu_name(),sizeof(info->name)-1);
            info->logical_processors=system_info_logical_processors();
            info->usage_percent=system_info_cpu_usage_percent();
            info->frequency_hz=system_info_tsc_frequency_hz();
            info->uptime_ms=system_info_uptime_ms();
            return 0;
        }
        case SYS_MEMORY_INFO: {
            struct memory_monitor_info *info=(struct memory_monitor_info*)(uintptr_t)a1;
            if(!info) return -1;
            info->total_bytes=system_info_total_ram_bytes();
            info->available_bytes=system_info_usable_ram_bytes();
            info->used_bytes=info->total_bytes>info->available_bytes
                ? info->total_bytes-info->available_bytes : 0;
            info->framebuffer_bytes=gop_get_framebuffer_size_bytes();
            return 0;
        }
        case SYS_DISK_STATS: {
            struct disk_monitor_info *info=(struct disk_monitor_info*)(uintptr_t)a1;
            if(!info) return -1;
            memset(info,0,sizeof(*info));
            info->device_count=block_device_count();
            for(uint32_t index=0;index<info->device_count;index++){
                struct storage_device_info device;
                if(!block_device_get_info(index,&device)) continue;
                if(device.operational) info->operational_count++;
                info->total_bytes+=device.sector_count*device.sector_size;
            }
            return 0;
        }
        case SYS_EXIT:
            serial_write_string("[SYSCALL] exit\n");
            gop_write("[SYSCALL] exit\n");
            for(;;) __asm__ volatile("cli; hlt");
        case SYS_SLEEP:
            if(a1>UINT32_MAX) return -1;
            // Real HW: PIT IRQ may not arrive via APIC – используем busy-wait чтобы не виснуть на hlt.
            // 1ms ~ 1e6 nop+pause, калибровка грубая но не требует timer_tick.
            for(volatile uint64_t wait=0;wait<a1*1000000ULL;wait++){
                __asm__ volatile("pause");
            }
            // Yield to scheduler so other threads run
            scheduler_yield();
            return 0;
        case SYS_REBOOT:
            power_reboot();
            return 0;
        case SYS_SHUTDOWN:
            power_shutdown();
            return 0;
        case SYS_BATTERY_INFO: {
            struct battery_info *out=(struct battery_info*)(uintptr_t)a1;
            if(!out) return -1;
            return power_battery_get(out) ? 0 : -1;
        }
        case SYS_AUDIO_GET_STATUS: {
            struct audio_status *out=(struct audio_status*)(uintptr_t)a1;
            if(!out) return -1;
            audio_get_status(out);
            return 0;
        }
        case SYS_AUDIO_GET_VOLUME:
            return audio_get_volume();
        case SYS_AUDIO_SET_VOLUME:
            if(a1>100) return -1;
            audio_set_volume((uint8_t)a1);
            return 0;
        case SYS_AUDIO_IS_MUTED:
            return audio_is_muted() ? 1 : 0;
        case SYS_AUDIO_SET_MUTED:
            audio_set_muted(a1!=0);
            return 0;
        case SYS_AUDIO_ADJUST_VOLUME:
            audio_adjust_volume((int8_t)(int64_t)a1);
            return 0;
        case SYS_AUDIO_PLAY_TEST_SOUND:
            audio_play_test_sound();
            return 0;
        case SYS_SCHED_YIELD:
            scheduler_yield();
            return 0;
        default:
            serial_write_string("[SYSCALL] unknown n="); print_hex(n); serial_write_string("\n");
            return -1;
    }
}

void syscall_init(void){
    static bool filesystem_checked;
    static bool audio_checked;
    // IDT 0x80 уже настроен в idt_init с DPL3 (0xEE)
    // используем klog если уже инициализирован, иначе fallback на serial
    // klog_inited проверяется через verbose флаг (если false после init, всё равно работает)
    // просто пишем DEBUG чтобы не спамить primary screen дважды (boot.c уже логирует)
    klog(KLOG_DEBUG, "syscall: int 0x80 handler ready (DPL3)");
    if(!audio_checked){
        audio_checked=true;
        audio_init();
        klog(KLOG_INFO, "audio: PC speaker backend ready");
    }
    if(!filesystem_checked){
        filesystem_checked=true;
        boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "storage: scanning PCI controllers");
        storage_probe_init();
        boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "storage: initializing block transports");
        (void)block_device_init();
        struct ahci_probe_stats ahci_stats;
        struct xhci_probe_stats xhci_stats;
        struct ehci_probe_stats ehci_stats;
        ahci_get_probe_stats(&ahci_stats);
        xhci_get_probe_stats(&xhci_stats);
        ehci_get_probe_stats(&ehci_stats);
        klogf(KLOG_INFO,"pci: %u storage/USB controller(s) detected",
              storage_controller_count());
        klogf(KLOG_INFO,"storage: %u operational disk(s) detected",block_device_count());
        klogf(KLOG_INFO,"ahci: %u SATA disk(s) identified",ahci_device_count());
        klogf(KLOG_DEBUG,"ahci: controllers=%u ports=%u sata=%u identify_failures=%u",
              ahci_stats.controllers,ahci_stats.implemented_ports,
              ahci_stats.sata_ports,ahci_stats.identify_failures);
        klogf(KLOG_INFO,"xhci: controllers=%u connected=%u addressed=%u usb-storage=%u failures=%u stage=%u",
              xhci_stats.controllers,xhci_stats.connected_ports,
              xhci_stats.addressed_devices,xhci_stats.mass_storage_devices,
              xhci_stats.failures,xhci_stats.last_stage);
        klogf(KLOG_INFO,"ehci: controllers=%u connected=%u high-speed=%u usb-storage=%u failures=%u stage=%u",
              ehci_stats.controllers,ehci_stats.connected_ports,
              ehci_stats.high_speed_ports,ehci_stats.mass_storage_devices,
              ehci_stats.failures,ehci_stats.last_stage);
        klog(KLOG_DEBUG,"usb stages: 1=pci 2=running 3=port 4=addressed 5=bulk-endpoints 6=scsi 7=ready");
        if(vfs_mount_root()){
            klogf(KLOG_OK,"vfs: root mounted from %s",vfs_root_device_name());
            // Читаем конфиги созданные установщиком (если есть) – для проверки инсталла
            {
                int32_t fd=vfs_open("/purec/install.cfg");
                if(fd>=0){
                    char cfg_buf[512]={0};
                    int32_t r=vfs_read(fd,cfg_buf,sizeof(cfg_buf)-1);
                    (void)vfs_close(fd);
                    if(r>0){
                        // лог без экрана? показываем кратко
                        klogf(KLOG_INFO,"config: /purec/install.cfg (%d bytes)",r);
                        // подробный дамп только в dmesg через serial? уже в klog
                        // печатаем построчно для читаемости
                        cfg_buf[r]=0;
                        // разбиваем по \n
                        char *p=cfg_buf;
                        while(*p){
                            char line[128]={0};
                            uint32_t i=0;
                            while(*p && *p!='\n' && i<127){ line[i++]=*p++; }
                            if(*p=='\n') p++;
                            if(line[0]) klogf(KLOG_INFO,"  cfg: %s",line);
                        }
                    }
                }
                fd=vfs_open("/etc/hostname");
                if(fd>=0){
                    char hn[64]={0};
                    int32_t r=vfs_read(fd,hn,sizeof(hn)-1);
                    (void)vfs_close(fd);
                    if(r>0){
                        hn[r]=0;
                        // trim newline
                        for(int i=0;i<r;i++) if(hn[i]=='\n'||hn[i]=='\r'){ hn[i]=0; break; }
                        klogf(KLOG_INFO,"config: hostname='%s'",hn);
                    }
                }
                fd=vfs_open("/boot/loader.cfg");
                if(fd>=0){
                    char lb[256]={0};
                    int32_t r=vfs_read(fd,lb,sizeof(lb)-1);
                    (void)vfs_close(fd);
                    if(r>0){
                        lb[r]=0;
                        klogf(KLOG_DEBUG,"config: /boot/loader.cfg %d bytes",r);
                    }
                }
            }
        } else {
            klog(KLOG_WARN,"vfs: no PURECOS FAT32 root found (use 'install' or 'mkfs.fat32' to create)");
        }
        boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "storage and filesystem probe complete");
    }
}
