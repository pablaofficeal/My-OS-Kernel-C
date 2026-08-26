#include "ps2_mouse.h"
#include "../pic.h"
#include "../serial.h"
#include "../gop.h"
#include "../vga.h"
#include <stdint.h>

// Linux psmouse упрощённо: 8042 + IRQ12, 3-байтный пакет
// Порты 8042
#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void io_wait(void){ outb(0x80,0); }

static void ps2_wait_input(void){ while(inb(PS2_STATUS)&2) {} }
static void ps2_wait_output(void){ while(!(inb(PS2_STATUS)&1)) {} }

static void ps2_write_cmd(uint8_t cmd){
    ps2_wait_input();
    outb(PS2_CMD, cmd);
}
static void ps2_write_data(uint8_t data){
    ps2_wait_input();
    outb(PS2_DATA, data);
}
static uint8_t ps2_read_data(void){
    ps2_wait_output();
    return inb(PS2_DATA);
}
// Отправка в мышь: D4 + data
static void mouse_write(uint8_t data){
    ps2_write_cmd(0xD4);
    ps2_write_data(data);
}
static uint8_t mouse_read_ack(void){
    uint8_t c = ps2_read_data();
    // ACK 0xFA, Resend 0xFE, Error 0xFC
    return c;
}

static struct mouse_state state = {.x=400,.y=300,.buttons=0};
static int32_t bound_w=1280, bound_h=800;
static uint8_t packet[4];
static int pkt_idx=0;
static bool has_mouse=false;
static int32_t old_x=400, old_y=300;
static bool first_draw=true;

// Сохраняем фон под курсором 12x12
#define CURS_W 12
#define CURS_H 12
static uint32_t bg_buf[CURS_W*CURS_H];
static uint32_t cursor_color = 0xFFFFFF;
static uint32_t cursor_border = 0x000000;

static void save_bg(int32_t x,int32_t y){
    // Заглушка: gop_draw_rect сам затрёт, для VGA тоже
    // Для GOP сохраняем через gop.addr напрямую если доступен
    extern struct gop_state* gop_get_state(void); // не экспонируем, просто рисуем поверх
    (void)x;(void)y;
}

void mouse_set_bounds(int32_t w,int32_t h){ bound_w=w; bound_h=h; if(state.x>=w) state.x=w-1; if(state.y>=h) state.y=h-1; }

struct mouse_state mouse_get_state(void){ return state; }

// Рисует курсор как в Linux: стрелка 12x12
static void draw_cursor(int32_t x,int32_t y){
    if(!gop_is_available()){
        // VGA text 80x25, пиксели -> символы
        int tx = x / 8;
        int ty = y / 16;
        int otx = old_x / 8;
        int oty = old_y / 16;
        volatile uint16_t *vga = (volatile uint16_t*)(uintptr_t)0xB8000;
        if(!first_draw){
            if(otx>=0 && otx<80 && oty>=0 && oty<25) vga[oty*80+otx] = (0x0F<<8)|' ';
            if(tx>=0 && tx<80 && ty>=0 && ty<25) vga[ty*80+tx] = (0x8F<<8)|0xDB;
        } else {
            if(tx>=0 && tx<80 && ty>=0 && ty<25) vga[ty*80+tx] = (0x8F<<8)|0xDB;
        }
        old_x=x; old_y=y;
        first_draw=false;
        return;
    }
    // Стираем старый курсор закраской фона (тёмный 0x1E1E2E)
    if(!first_draw){
        gop_draw_rect(old_x, old_y, CURS_W, CURS_H, 0x1E1E2E);
    }
    first_draw=false;
    // Рисуем новый курсор: белая стрелка с чёрной рамкой
    // Простая стрелка: треугольник
    for(int dy=0;dy<CURS_H;dy++){
        for(int dx=0;dx<CURS_W;dx++){
            bool inside = false;
            if(dy==0 && dx<8) inside=true;
            else if(dy<8 && dx<=dy) inside=true;
            else if(dy>=8 && dy<10 && dx<3) inside=true;
            if(inside){
                // рамка
                bool border = (dx==0 || dy==0 || dx==dy || (dy>=8 && (dx==0||dx==2)));
                uint32_t c = border ? cursor_border : cursor_color;
                // gop_put_pixel через gop_draw_rect точкой
                gop_draw_rect(x+dx, y+dy, 1,1,c);
            }
        }
    }
    old_x=x; old_y=y;
}

void ps2_mouse_handler(void){
    uint8_t status = inb(PS2_STATUS);
    if(!(status & 1)) return;
    // Проверяем что это от мыши (bit5 = mouse)
    // На некоторых эмуляторах bit5 не ставится, читаем в любом случае
    uint8_t data = inb(PS2_DATA);

    // Первый байт должен иметь bit3=1
    if(pkt_idx==0 && (data & 0x08)==0){
        // Десинхрон, сбросим
        // Не сбрасываем если это ACK 0xFA во время init
        return;
    }
    packet[pkt_idx++] = data;
    if(pkt_idx==3){
        // Декодируем как в Linux drivers/input/mouse/psmouse-base.c
        uint8_t b0 = packet[0];
        int8_t dx = (int8_t)packet[1];
        int8_t dy = (int8_t)packet[2];
        // Обработка переполнения
        if(b0 & 0x40) {} // X overflow
        if(b0 & 0x80) {} // Y overflow
        // Движение
        state.dx = dx;
        state.dy = -dy; // Y инвертируем (мышь даёт - при движении вверх)
        state.x += dx;
        state.y += -dy;
        if(state.x<0) state.x=0;
        if(state.y<0) state.y=0;
        if(state.x >= bound_w - CURS_W) state.x = bound_w - CURS_W -1;
        if(state.y >= bound_h - CURS_H) state.y = bound_h - CURS_H -1;
        state.buttons = b0 & 0x07;
        state.has_data = true;

        // Рисуем через GOP
        draw_cursor(state.x, state.y);

        pkt_idx=0;
        // ACK для отладки
        // serial_write_string("[MOUSE] x="); ...
    }
    // EOI уже в isr_handler, но для IRQ12 нужно ещё pic_eoi
}

void ps2_mouse_init(void){
    serial_write_string("[MOUSE] init PS/2 (Linux psmouse style)...\n");

    // Включаем мышь через 8042, как в Linux
    // 1. Включить AUX
    ps2_write_cmd(0xA8);
    io_wait();
    // 2. Прочитать Command Byte
    ps2_write_cmd(0x20);
    ps2_wait_output();
    uint8_t status = inb(PS2_DATA);
    serial_write_string("[MOUSE] cmd byte read\n");
    // Включить IRQ12 (bit1), отключить clock мыши? bit5
    status |= 0x02; // enable IRQ12
    status &= ~0x20; // enable mouse
    // Записать обратно
    ps2_write_cmd(0x60);
    ps2_write_data(status);
    io_wait();

    // 3. Сброс мыши
    mouse_write(0xFF);
    uint8_t ack = mouse_read_ack();
    if(ack==0xFA){
        // Ждём自检 0xAA 0x00
        uint8_t bat = ps2_read_data(); // 0xAA
        uint8_t id = ps2_read_data(); // 0x00
        (void)bat; (void)id;
        serial_write_string("[MOUSE] reset ok\n");
    }
    // 4. Set defaults
    mouse_write(0xF6);
    mouse_read_ack();
    // 5. Включить поток данных
    mouse_write(0xF4);
    uint8_t ack2 = mouse_read_ack();
    if(ack2==0xFA){
        has_mouse=true;
        serial_write_string("[MOUSE] enabled, IRQ12 active\n");
    } else {
        serial_write_string("[MOUSE] enable fail\n");
    }

    // Размаскировать IRQ12 (и каскад IRQ2)
    // pic_mask_all уже, теперь разрешим 2 и 12
    // 0x21 master, 0xA1 slave
    uint8_t m1 = inb(0x21);
    uint8_t m2 = inb(0xA1);
    m1 &= ~(1<<2); // cascade
    m2 &= ~(1<<4); // IRQ12 -> slave bit4
    outb(0x21, m1);
    outb(0xA1, m2);
    serial_write_string("[MOUSE] PIC unmasked 2+12\n");

    if(gop_is_available()){
        bound_w = gop_get_width();
        bound_h = gop_get_height();
        if(bound_w==0) bound_w=1280;
        if(bound_h==0) bound_h=800;
    } else {
        bound_w = 80*8; bound_h = 25*16; // VGA text в пикселях
    }
    state.x = bound_w/2;
    state.y = bound_h/2;
    old_x = state.x; old_y = state.y;
    draw_cursor(state.x, state.y);
    pkt_idx=0;
}
