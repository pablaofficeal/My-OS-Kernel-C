#include "keyboard.h"
#include "../kernel/klog.h"
#include "serial.h"
#include <stdint.h>
#include <stdbool.h>

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define KBD_CMD 0x64

static inline void outb(uint16_t port, uint8_t val){ __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline uint8_t inb(uint16_t port){ uint8_t ret; __asm__ volatile("inb %1,%0":"=a"(ret):"Nd"(port)); return ret; }
static inline void io_wait(void){ outb(0x80,0); }

static bool shift_pressed = false;
static bool caps_lock = false;

// US QWERTY scancode set 1, без shift
static const char scancode_map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char scancode_shift_map[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

#define KBD_BUF_SIZE 128
static char kbd_buf[KBD_BUF_SIZE];
static uint8_t kbd_head = 0, kbd_tail = 0, kbd_count = 0;

static void kbd_push(char c){
    if(kbd_count >= KBD_BUF_SIZE) return;
    kbd_buf[kbd_head] = c;
    kbd_head = (kbd_head + 1) % KBD_BUF_SIZE;
    kbd_count++;
}

static bool kbd_pop(char *out){
    if(kbd_count==0) return false;
    *out = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    kbd_count--;
    return true;
}

static void handle_scancode(uint8_t sc){
    // debug: log scancode to serial for QMP sendkey debugging
    // serial_write_string("[KBD] sc=0x"); // too noisy, only for first few
    if(sc == 0x2A || sc == 0x36){ // LSHIFT / RSHIFT press
        shift_pressed = true;
        return;
    }
    if(sc == 0xAA || sc == 0xB6){ // shift release
        shift_pressed = false;
        return;
    }
    if(sc == 0x3A){ // caps
        caps_lock = !caps_lock;
        return;
    }
    if(sc & 0x80){
        // release of other keys, ignore
        return;
    }
    if(sc >= 128) return;
    char c = 0;
    bool upper = shift_pressed ^ caps_lock;
    char base = scancode_map[sc];
    if(base >= 'a' && base <= 'z'){
        c = upper ? scancode_shift_map[sc] : base;
    } else {
        c = shift_pressed ? scancode_shift_map[sc] : base;
        if(!c) c = base;
    }
    if(c){
        kbd_push(c);
    }
}

void keyboard_poll(void){
    while(inb(KBD_STATUS) & 1){
        uint8_t sc = inb(KBD_DATA);
        handle_scancode(sc);
    }
}

bool keyboard_has_key(void){
    keyboard_poll();
    return kbd_count > 0;
}

bool keyboard_try_getc(char *out){
    keyboard_poll();
    return kbd_pop(out);
}

char keyboard_getc(void){
    char c;
    while(!keyboard_try_getc(&c)){
        __asm__ volatile("pause");
        keyboard_poll();
    }
    return c;
}

void keyboard_init(void){
    // включаем клавиатуру через 8042 (как в Linux atkbd)
    // 0xAE = enable first PS/2 port (keyboard)
    __asm__ volatile("cli");
    outb(KBD_CMD, 0xAE);
    io_wait();
    while(inb(KBD_STATUS) & 1) inb(KBD_DATA);
    // оставляем IRQ1 замаскированным для polling (чтобы не триггерить vector 33 без handler)
    uint8_t mask = inb(0x21);
    mask |= (1<<1);
    outb(0x21, mask);
    __asm__ volatile("sti");
    klog(KLOG_INFO, "keyboard: PS/2 polling ready (US layout)");
}

void keyboard_set_leds(bool caps, bool num, bool scroll){
    (void)caps; (void)num; (void)scroll;
    // TODO: send 0xED + leds
}
