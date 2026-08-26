#include "syscall.h"
#include "../drivers/serial.h"
#include "../drivers/gop.h"
#include "../drivers/vga.h"
#include <stdint.h>

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
    // IDT 0x80 уже настроен в idt_init с DPL3 (0xEE)
    serial_write_string("[SYSCALL] init, int 0x80 ready\n");
}
