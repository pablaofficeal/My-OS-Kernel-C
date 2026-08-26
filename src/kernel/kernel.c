#include "kernel.h"
#include "../drivers/gop.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/mouse/i2c_hid_touchpad.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../kernel/syscall.h"
#include "../lib/string.h"

static inline int64_t do_syscall(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5){
    int64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "r10","r8","memory");
    return ret;
}

static void halt_forever(void){
    // HLT возвращает управление после IRQ12; cli здесь отключал мышь навсегда.
    if(i2c_hid_touchpad_is_active()){
        // I2C1 GPIO interrupt (GSI 40) has no IOAPIC route yet, so the
        // target touchpad is polled until APIC input routing is added.
        for(;;) { i2c_hid_touchpad_poll(); __asm__ volatile("pause"); }
    }
    for(;;) __asm__ volatile("sti; hlt");
}

void kernel_main(struct limine_framebuffer *fb) {
    // Limine уже в long mode, gdt/idt уже настроены в boot.c, но переинициализируем для консистентности
    gop_init_from_limine(fb);
    // serial уже init в boot.c, но на всякий
    serial_write_string("[KERNEL] Limine GOP init\n");
    if(gop_is_available()){
        gop_clear(0x1E1E2E);
        gop_set_color(0xCDD6F4, 0x1E1E2E);
        gop_write("PureC OS 64-bit [Limine+GOP]\n");
        gop_write("================================\n");
        gop_write("GDT64: OK  IDT64: OK  GOP: OK\n");
    } else {
        vga_write("PureC OS 64-bit [Limine] no GOP, VGA\n");
    }

    // GDT/IDT уже, но проверим
    serial_write_string("[TEST] int3\n");
    __asm__ volatile("int3");
    if(gop_is_available()) gop_write("[OK] int3 handled\n");
    serial_write_string("[OK] int3\n");

    syscall_init();
    const char *msg="Hello from syscall (Limine)!\n";
    do_syscall(SYS_WRITE, (uint64_t)msg, 27, 1,0,0);
    if(gop_is_available()) gop_write("syscall WRITE done\n");

    // GOP тесты через syscalls
    do_syscall(SYS_DRAW_RECT, 50,50,300,80, 0xFF0000); // но helper ждет 5 args, последний цвет в rdi, h в rsi
    // Правильно: x=50,y=50,w=300,h=80,color=0x00FF00
    __asm__ volatile("mov $100, %%rbx; mov $50, %%rcx; mov $300, %%rdx; mov $80, %%rsi; mov $0x00FF00, %%rdi; mov $100, %%rax; int $0x80" ::: "rax","rbx","rcx","rdx","rsi","rdi","r10","r8","memory");
    if(gop_is_available()) gop_write("DRAW_RECT via syscall done\n");

    // kernel_main очищает framebuffer после инициализации PS/2.
    // Восстанавливаем курсор и его диагностику поверх готового интерфейса.
    mouse_redraw();
    i2c_hid_touchpad_redraw();

    serial_write_string("[KERNEL] halt\n");
    halt_forever();
}
