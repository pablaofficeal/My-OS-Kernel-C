#include "syscall.h"
#include "klog.h"
#include "../drivers/serial.h"
#include "../drivers/gop.h"
#include "../drivers/vga.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/storage/block_device.h"
#include "../drivers/storage/ahci.h"
#include "../drivers/storage/storage_probe.h"
#include "../fs/fat32.h"
#include <stdint.h>
#include <stdbool.h>

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
        case SYS_GETPID:
            return 42;
        case SYS_GET_MOUSE: {
            struct mouse_state *out = (struct mouse_state*)(uintptr_t)a1;
            if(!out) return -1;
            *out = mouse_get_state();
            return 0;
        }
        case SYS_FILE_OPEN:
            return fat32_open((const char*)(uintptr_t)a1);
        case SYS_FILE_READ:
            return fat32_read((int32_t)a1,(void*)(uintptr_t)a2,(uint32_t)a3);
        case SYS_FILE_DELETE:
            return fat32_delete((const char*)(uintptr_t)a1);
        case SYS_FILE_RENAME:
            return fat32_rename((const char*)(uintptr_t)a1,
                                (const char*)(uintptr_t)a2);
        case SYS_FILE_MOVE:
            return fat32_move((const char*)(uintptr_t)a1,
                              (const char*)(uintptr_t)a2);
        case SYS_DIR_LIST:
            return fat32_list((const char*)(uintptr_t)a1,
                              (struct fs_directory_entry*)(uintptr_t)a2,
                              (uint32_t)a3);
        case SYS_FILE_CREATE:
            return fat32_create_file((const char*)(uintptr_t)a1);
        case SYS_DIR_CREATE:
            return fat32_create_directory((const char*)(uintptr_t)a1);
        case SYS_DISK_LIST:
            return block_device_list((struct storage_device_info*)(uintptr_t)a1,
                                     (uint32_t)a2);
        case SYS_STORAGE_CONTROLLERS:
            return storage_controller_list(
                (struct storage_controller_info*)(uintptr_t)a1,(uint32_t)a2);
        case SYS_FAT32_FORMAT:
            return fat32_format_device((const char*)(uintptr_t)a1,
                                       (const char*)(uintptr_t)a2,
                                       (const char*)(uintptr_t)a3);
        case SYS_EXIT:
            serial_write_string("[SYSCALL] exit\n");
            gop_write("[SYSCALL] exit\n");
            for(;;) __asm__ volatile("cli; hlt");
        case SYS_SLEEP:
            for(volatile uint64_t i=0;i<a1*1000000ULL;i++) __asm__ volatile("nop");
            return 0;
        default:
            serial_write_string("[SYSCALL] unknown n="); print_hex(n); serial_write_string("\n");
            return -1;
    }
}

void syscall_init(void){
    static bool filesystem_checked;
    // IDT 0x80 уже настроен в idt_init с DPL3 (0xEE)
    // используем klog если уже инициализирован, иначе fallback на serial
    // klog_inited проверяется через verbose флаг (если false после init, всё равно работает)
    // просто пишем DEBUG чтобы не спамить primary screen дважды (boot.c уже логирует)
    klog(KLOG_DEBUG, "syscall: int 0x80 handler ready (DPL3)");
    if(!filesystem_checked){
        filesystem_checked=true;
        storage_probe_init();
        (void)block_device_init();
        struct ahci_probe_stats ahci_stats;
        ahci_get_probe_stats(&ahci_stats);
        klogf(KLOG_INFO,"pci-storage: %u AHCI/NVMe controller(s) detected",
              storage_controller_count());
        klogf(KLOG_INFO,"storage: %u operational disk(s) detected",block_device_count());
        klogf(KLOG_INFO,"ahci: %u SATA disk(s) identified",ahci_device_count());
        klogf(KLOG_DEBUG,"ahci: controllers=%u ports=%u sata=%u identify_failures=%u",
              ahci_stats.controllers,ahci_stats.implemented_ports,
              ahci_stats.sata_ports,ahci_stats.identify_failures);
        if(fat32_init()){
            klogf(KLOG_OK,"fat32: mounted from %s",fat32_device_name());
        } else {
            klog(KLOG_WARN,"fat32: no PURECOS FAT32 volume found");
        }
    }
}
