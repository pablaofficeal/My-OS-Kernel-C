#include "userspace.h"
#include "../drivers/gop.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../kernel/klog.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stdbool.h>

// цвета как в Catppuccin, как у boot log
#define DESKTOP_BG  0x181825
#define TOPBAR_BG   0x313244
#define TOPBAR_FG   0xCDD6F4
#define TOPBAR_H    28
#define WINDOW_BG   0x1E1E2E
#define WINDOW_BORDER 0x45475A
#define TITLE_BG    0x89B4FA
#define TITLE_FG    0x1E1E2E
#define TITLE_H     28
#define TERM_BG     0x1E1E2E
#define TERM_FG     0xCDD6F4
#define PROMPT_FG   0xA6E3A1
#define PROMPT_STR  "purec@os:~$ "

// окно терминала
static uint32_t win_x, win_y, win_w, win_h;
static uint32_t term_x, term_y, term_w, term_h;
static uint32_t term_cur_x, term_cur_y;
static uint32_t term_cols, term_rows;
static uint32_t desktop_w, desktop_h;

static void draw_desktop(void){
    desktop_w = gop_get_width();
    desktop_h = gop_get_height();
    if(desktop_w==0) desktop_w=1280;
    if(desktop_h==0) desktop_h=800;
    gop_clear(DESKTOP_BG);
    // topbar как в GNOME/KDE
    gop_draw_rect(0, 0, desktop_w, TOPBAR_H, TOPBAR_BG);
    gop_draw_text_at(12, 8, "PureC OS", 0x89B4FA, TOPBAR_BG);
    gop_draw_text_at(120, 8, "Userspace v0.1.0", TOPBAR_FG, TOPBAR_BG);
    // справа - статус
    const char *status = " [ boot log hidden ]  F1:dmesg  F2:clear  F12:debug";
    // вычисляем x справа
    uint32_t status_len = strlen(status);
    uint32_t status_x = desktop_w - status_len*8 - 12;
    if(status_x < 200) status_x = 200;
    gop_draw_text_at(status_x, 8, status, 0x9399B2, TOPBAR_BG);
}

static void draw_window_chrome(void){
    // окно по центру ниже topbar
    win_w = 720;
    win_h = 400;
    if(desktop_w < win_w+40) win_w = desktop_w - 40;
    if(desktop_h < win_h+60) win_h = desktop_h - 60;
    win_x = (desktop_w - win_w)/2;
    win_y = TOPBAR_H + (desktop_h - TOPBAR_H - win_h)/2;

    // тень (просто смещённый прямоугольник)
    gop_draw_rect(win_x+4, win_y+4, win_w, win_h, 0x11111B);
    // фон окна + бордер
    gop_draw_rect(win_x, win_y, win_w, win_h, WINDOW_BORDER);
    gop_draw_rect(win_x+1, win_y+1, win_w-2, win_h-2, WINDOW_BG);
    // title bar
    gop_draw_rect(win_x+1, win_y+1, win_w-2, TITLE_H, TITLE_BG);
    gop_draw_text_at(win_x+12, win_y+8, "Terminal — purec@os", TITLE_FG, TITLE_BG);
    // кнопки окна (декор)
    gop_draw_rect(win_x+win_w-52, win_y+7, 16, 16, 0xF38BA8); // close red
    gop_draw_rect(win_x+win_w-32, win_y+7, 16, 16, 0xF9E2AF); // min yellow
    gop_draw_text_at(win_x+win_w-48, win_y+8, "x", 0x1E1E2E, 0xF38BA8);
    gop_draw_text_at(win_x+win_w-28, win_y+8, "-", 0x1E1E2E, 0xF9E2AF);

    // term area внутри окна
    term_x = win_x + 12;
    term_y = win_y + TITLE_H + 12;
    term_w = win_w - 24;
    term_h = win_h - TITLE_H - 24;
    term_cols = term_w / 8;
    term_rows = term_h / 10;
    term_cur_x = term_x;
    term_cur_y = term_y;
    // очищаем term area
    gop_draw_rect(term_x, term_y, term_w, term_h, TERM_BG);
}

static void term_scroll(void){
    // скрываем курсор мыши на время скролла чтобы не испортить save_bg
    // делаем сдвиг внутри term area на 10px вверх
    const uint32_t line_h = 10;
    if(term_h <= line_h) return;

    // копируем пиксели внутри окна (только term area)
    // используем gop_get_pixel/put_pixel для простоты, но можно memcpy по строкам
    // для скорости делаем построчно через gop.addr напрямую если доступно
    // но используем gop API с проверкой available
    // делаем ручной сдвиг: для каждой y в term area копируем строку y+line_h -> y
    for(uint32_t y = 0; y + line_h < term_h; y++){
        for(uint32_t x = 0; x < term_w; x++){
            uint32_t c = gop_get_pixel(term_x + x, term_y + y + line_h);
            gop_put_pixel(term_x + x, term_y + y, c);
        }
    }
    // чистим нижнюю строку
    gop_draw_rect(term_x, term_y + term_h - line_h, term_w, line_h, TERM_BG);
    if(term_cur_y >= line_h) term_cur_y -= line_h;
    else term_cur_y = term_y;
}

static void term_putc(char c){
    if(c == '\r'){
        term_cur_x = term_x;
        return;
    }
    if(c == '\n'){
        term_cur_x = term_x;
        term_cur_y += 10;
        if(term_cur_y + 8 >= term_y + term_h) term_scroll();
        return;
    }
    if(c == '\b'){
        if(term_cur_x > term_x){
            term_cur_x -= 8;
            // стираем символ фоном
            gop_draw_rect(term_cur_x, term_cur_y, 8, 8, TERM_BG);
        }
        return;
    }
    if(term_cur_x + 8 >= term_x + term_w){
        term_cur_x = term_x;
        term_cur_y += 10;
        if(term_cur_y + 8 >= term_y + term_h) term_scroll();
    }
    if(term_cur_y + 8 >= term_y + term_h) term_scroll();
    // рисуем символ напрямую через gop_draw_text_at с текущими цветами
    // для простоты используем gop_draw_text_at для одного символа
    char tmp[2] = {c, 0};
    gop_draw_text_at(term_cur_x, term_cur_y, tmp, TERM_FG, TERM_BG);
    term_cur_x += 8;
}

static void term_write(const char *s){
    while(*s) term_putc(*s++);
}

static void term_write_colored(const char *s, uint32_t fg){
    // временно меняем цвет через gop_set_color не подходит, так как term_putc использует gop_draw_text_at с TERM_FG
    // для цветного вывода делаем ручной draw
    // упростим: меняем глобальный TERM_FG временно через замену в gop_draw_text_at вызовах
    // но term_putc хардкодит TERM_FG, поэтому для цвета выводим напрямую
    while(*s){
        if(*s=='\n' || *s=='\r' || *s=='\b'){
            term_putc(*s++);
            continue;
        }
        if(term_cur_x + 8 >= term_x + term_w){
            term_cur_x = term_x;
            term_cur_y += 10;
            if(term_cur_y + 8 >= term_y + term_h) term_scroll();
        }
        if(term_cur_y + 8 >= term_y + term_h) term_scroll();
        char tmp[2] = {*s, 0};
        gop_draw_text_at(term_cur_x, term_cur_y, tmp, fg, TERM_BG);
        term_cur_x += 8;
        s++;
    }
}

static void term_clear(void){
    gop_draw_rect(term_x, term_y, term_w, term_h, TERM_BG);
    term_cur_x = term_x;
    term_cur_y = term_y;
}

static void term_putc_cb(char c){ term_putc(c); }

static void term_prompt(void){
    term_write_colored(PROMPT_STR, PROMPT_FG);
}

// shell
#define SHELL_BUF 256
static char shell_buf[SHELL_BUF];
static int shell_len = 0;

static void shell_help(void){
    term_write("Available commands:\n");
    term_write("  help     - show this help\n");
    term_write("  clear    - clear terminal\n");
    term_write("  dmesg    - show boot log (klog)\n");
    term_write("  about    - about PureC OS\n");
    term_write("  demo     - draw demo rectangles\n");
    term_write("  debug    - toggle mouse debug overlay\n");
    term_write("  reboot   - halt system\n");
    term_write("  echo <text> - print text\n");
}

static void shell_about(void){
    term_write("PureC OS 64-bit v0.1.0 [Limine+GOP]\n");
    term_write("Kernel: GDT/IDT/PIC/PS2 mouse, klog boot screen\n");
    term_write("Userspace: primitive desktop + shell\n");
    term_write("Resolution: ");
    char tmp[32];
    // простая конвертация без printf
    uint32_t w = gop_get_width(), h = gop_get_height();
    // рисуем числа через term_putc с конвертацией
    // используем klog helper? просто руками
    char buf[16]; int i=0; uint32_t t=w; if(t==0) buf[i++]='0'; else{char b[10];int bl=0;while(t>0){b[bl++]='0'+t%10; t/=10;}while(bl--) buf[i++]=b[bl];} buf[i]=0; term_write(buf);
    term_write("x");
    i=0; t=h; if(t==0) buf[i++]='0'; else{char b[10];int bl=0;while(t>0){b[bl++]='0'+t%10; t/=10;}while(bl--) buf[i++]=b[bl];} buf[i]=0; term_write(buf);
    term_write("  Userspace: window 720x400\n");
    (void)tmp;
}

void userspace_exec_command(const char *cmd){
    while(*cmd==' ' || *cmd=='\t') cmd++;
    if(*cmd==0) return;
    if(strcmp(cmd, "help")==0){
        shell_help();
    } else if(strcmp(cmd, "clear")==0){
        term_clear();
    } else if(strcmp(cmd, "dmesg")==0){
        term_write("--- dmesg ---\n");
        klog_dump_with(term_putc_cb);
        term_write("\n--- end dmesg ---\n");
    } else if(strcmp(cmd, "about")==0){
        shell_about();
    } else if(strcmp(cmd, "demo")==0){
        term_write("Drawing demo rectangles in desktop...\n");
        // рисуем два прямоугольника внизу десктопа, поверх, но не в окне
        uint32_t y = desktop_h - 80;
        gop_draw_rect(40, y, 300, 50, 0xF38BA8);
        gop_draw_rect(400, y, 300, 50, 0xA6E3A1);
        gop_draw_text_at(120, y+18, "DEMO RED", 0x1E1E2E, 0xF38BA8);
        gop_draw_text_at(480, y+18, "DEMO GREEN", 0x1E1E2E, 0xA6E3A1);
        // перерисуем курсор
        mouse_redraw();
    } else if(strcmp(cmd, "debug")==0){
        bool cur = mouse_get_debug_overlay();
        mouse_set_debug_overlay(!cur);
        term_write(cur ? "Mouse debug overlay OFF\n" : "Mouse debug overlay ON\n");
        if(!cur) mouse_redraw(); else {
            // скрываем оверлей: перерисовываем десктоп
            draw_desktop();
            draw_window_chrome();
            term_write("Overlay hidden, desktop redrawn. Type 'clear'.\n");
            term_prompt();
        }
    } else if(strcmp(cmd, "reboot")==0){
        term_write("Halting system...\n");
        for(;;) __asm__ volatile("cli; hlt");
    } else if(strncmp(cmd, "echo ", 5)==0){
        term_write(cmd+5);
        term_write("\n");
    } else {
        term_write("Unknown command: ");
        term_write(cmd);
        term_write("\nType 'help' for list.\n");
    }
}

static void shell_handle_input(char c){
    if(c == '\n' || c == '\r'){
        term_putc('\n');
        shell_buf[shell_len] = 0;
        userspace_exec_command(shell_buf);
        shell_len = 0;
        term_prompt();
    } else if(c == '\b' || c == 127){
        if(shell_len > 0){
            shell_len--;
            term_putc('\b');
        }
    } else if(shell_len < SHELL_BUF-1){
        shell_buf[shell_len++] = c;
        term_putc(c);
    }
}

void userspace_init(void){
    keyboard_init();
    draw_desktop();
    draw_window_chrome();
    term_write("Welcome to PureC OS Userspace v0.1.0\n");
    term_write("Boot log hidden. Type 'help' for commands, 'dmesg' for boot log.\n");
    term_write("Primitive shell ready. Use keyboard to type.\n");
    term_write("\n");
    term_prompt();
    shell_len = 0;
    // мышь уже настроена, но нужно обновить границы под десктоп
    mouse_set_bounds(desktop_w, desktop_h);
    mouse_redraw();
    // убедимся что klog на экран выключен
    klog_set_screen_enabled(false);
}

void userspace_run(void){
    // основной цикл userspace как в Linux init -> shell
    for(;;){
        // опрос мыши (как в idle_forever) + клавиатуры
        ps2_mouse_poll();
        keyboard_poll();

        // обработка клавиатуры
        char c;
        while(keyboard_try_getc(&c)){
            // handle F-keys: F1=dmesg, F2=clear, F12=debug (scancodes: F1 0x3B, F2 0x3C, F12 0x58? но через polling scancode без 0xE0)
            // scancodes F1=0x3B, F2=0x3C, F12=0x58 (make), break = +0x80
            // наш keyboard driver транслирует только printable, F-keys игнорируются (возвращают 0)
            // поэтому они не попадут сюда. Для F-keys нужно отдельная обработка в keyboard.c
            // пока обрабатываем обычные символы
            shell_handle_input(c);
        }

        // можно добавить обработку нажатий мыши: клик в окне фокус и т.д.
        // пока просто держим курсор

        // лёгкая задержка чтобы не грузить CPU
        __asm__ volatile("pause");
        // небольшой sleep чтобы не спамить
        for(volatile int i=0;i<10000;i++) __asm__ volatile("nop");
    }
}
