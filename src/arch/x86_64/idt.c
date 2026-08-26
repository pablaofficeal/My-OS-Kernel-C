#include "idt.h"
#include "../../drivers/serial.h"
#include "../../drivers/fb.h"
#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void isr_stub_0(void);
extern void isr_stub_3(void);
extern void isr_stub_8(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_32(void);
extern void idt_load(uint64_t);

void idt_set_gate(int n, uint64_t handler, uint8_t flags) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;
    idt[n].ist         = 0;
    idt[n].type_attr   = flags;
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero        = 0;
}

static void print_hex(uint64_t v){
    const char *h="0123456789ABCDEF";
    char buf[17];
    buf[16]=0;
    for(int i=15;i>=0;i--){ buf[i]=h[v&0xF]; v>>=4; }
    serial_write_string("0x");
    serial_write_string(buf);
}

void isr_handler(uint64_t vector, uint64_t err, uint64_t rip, uint64_t cs, uint64_t rflags) {
    if (vector == 3) {
        serial_write_string("[IDT] #BP (int3) caught, vector=3 rip=");
        print_hex(rip); serial_write_string(" cs="); print_hex(cs); serial_write_string(" rflags="); print_hex(rflags); serial_write_string("\n");
        // fb_write_string("[IDT] #BP handled\n");
        return;
    }
    serial_write_string("[IDT] exception vector=");
    char buf[4];
    buf[0] = '0' + (vector/100)%10;
    buf[1] = '0' + (vector/10)%10;
    buf[2] = '0' + (vector%10);
    buf[3] = 0;
    if (vector < 10) { buf[0]=buf[2]; buf[1]=0; }
    else if (vector < 100) { buf[0]=buf[1]; buf[1]=buf[2]; buf[2]=0; }
    serial_write_string(buf);
    serial_write_string(" err="); print_hex(err);
    serial_write_string(" rip="); print_hex(rip);
    serial_write_string(" cs="); print_hex(cs);
    serial_write_string(" rflags="); print_hex(rflags);
    serial_write_string("\n");
    // fb disabled for GRUB test to avoid fb_addr fault
    // fb_write_string("[EXC] vector ");
    // fb_write_string(buf);
    // fb_write_string(" -> halt\n");
    for(;;) __asm__ volatile("cli; hlt");
}

void idt_init(void) {
    for(int i=0;i<256;i++) {
        idt[i].offset_low=0; idt[i].selector=0; idt[i].ist=0;
        idt[i].type_attr=0; idt[i].offset_mid=0; idt[i].offset_high=0; idt[i].zero=0;
    }
    // Заполняем ВСЕ 256 векторов дефолтом, иначе любой неожиданный IRQ => triple fault => Guru Meditation в VBox
    for(int i=0;i<256;i++) idt_set_gate(i, (uint64_t)isr_stub_0, 0x8E);
    idt_set_gate(3,  (uint64_t)isr_stub_3,  0x8E);
    idt_set_gate(8,  (uint64_t)isr_stub_8,  0x8E);
    idt_set_gate(13, (uint64_t)isr_stub_13, 0x8E);
    idt_set_gate(14, (uint64_t)isr_stub_14, 0x8E);
    idt_set_gate(32, (uint64_t)isr_stub_32, 0x8E);
    idtp.limit = sizeof(idt)-1;
    idtp.base  = (uint64_t)&idt;
    idt_load((uint64_t)&idtp);
}
