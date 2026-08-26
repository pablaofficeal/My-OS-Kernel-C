#include "terminal.h"
#include "commands.h"
#include "../../drivers/gop.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#define WINDOW_BG       0x1E1E2E
#define WINDOW_BORDER   0x45475A
#define WINDOW_SHADOW   0x11111B
#define TITLE_BG        0x89B4FA
#define TITLE_FG        0x1E1E2E
#define TERM_BG         0x1E1E2E
#define TERM_FG         0xCDD6F4
#define PROMPT_FG       0xA6E3A1
#define CLOSE_BG        0xF38BA8
#define MINIMIZE_BG     0xF9E2AF

#define TITLE_HEIGHT    34
#define GLYPH_SIZE      10
#define LINE_HEIGHT     13
#define WINDOW_TOP      140
#define WINDOW_MAX_W    960
#define WINDOW_MAX_H    580
#define SHELL_BUFFER_SIZE 256
#define PROMPT_TEXT     "purec@os:~$ "

static uint32_t window_x, window_y, window_w, window_h;
static uint32_t text_x, text_y, text_w, text_h;
static uint32_t cursor_x, cursor_y;
static char input_buffer[SHELL_BUFFER_SIZE];
static uint32_t input_length;

static void terminal_scroll(void){
    if(text_h<=LINE_HEIGHT) return;
    gop_scroll_rect_up(text_x, text_y, text_w, text_h, LINE_HEIGHT, TERM_BG);
    if(cursor_y>=text_y+LINE_HEIGHT) cursor_y-=LINE_HEIGHT;
    else cursor_y=text_y;
}

static void ensure_cursor_visible(void){
    if(cursor_y+GLYPH_SIZE>text_y+text_h) terminal_scroll();
}

static void putc_colored(char c, uint32_t color){
    if(c=='\r'){
        cursor_x=text_x;
        return;
    }
    if(c=='\n'){
        cursor_x=text_x;
        cursor_y+=LINE_HEIGHT;
        ensure_cursor_visible();
        return;
    }
    if(c=='\b'){
        if(cursor_x>text_x){
            cursor_x-=GLYPH_SIZE;
            gop_draw_rect(cursor_x, cursor_y, GLYPH_SIZE, GLYPH_SIZE, TERM_BG);
        }
        return;
    }
    if(c=='\t'){
        for(uint32_t i=0;i<4;i++) putc_colored(' ',color);
        return;
    }
    if(cursor_x+GLYPH_SIZE>text_x+text_w){
        cursor_x=text_x;
        cursor_y+=LINE_HEIGHT;
        ensure_cursor_visible();
    }
    char glyph[2]={c,0};
    gop_draw_text_sized_at(cursor_x,cursor_y,glyph,color,TERM_BG,GLYPH_SIZE);
    cursor_x+=GLYPH_SIZE;
}

static void write_unsigned(uint64_t value, uint32_t base, bool uppercase){
    const char *digits=uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[32];
    uint32_t length=0;
    if(value==0){ terminal_putc('0'); return; }
    while(value && length<sizeof(buffer)){
        buffer[length++]=digits[value%base];
        value/=base;
    }
    while(length) terminal_putc(buffer[--length]);
}

static void write_signed(int64_t value){
    if(value<0){
        terminal_putc('-');
        write_unsigned((uint64_t)(-(value+1))+1,10,false);
    } else {
        write_unsigned((uint64_t)value,10,false);
    }
}

static void draw_window(void){
    gop_draw_rect(window_x+5,window_y+5,window_w,window_h,WINDOW_SHADOW);
    gop_draw_rect(window_x,window_y,window_w,window_h,WINDOW_BORDER);
    gop_draw_rect(window_x+1,window_y+1,window_w-2,window_h-2,WINDOW_BG);
    gop_draw_rect(window_x+1,window_y+1,window_w-2,TITLE_HEIGHT,TITLE_BG);
    gop_draw_text_sized_at(window_x+14,window_y+11,"Terminal - purec@os",
                           TITLE_FG,TITLE_BG,GLYPH_SIZE);

    gop_draw_rect(window_x+window_w-58,window_y+8,18,18,CLOSE_BG);
    gop_draw_rect(window_x+window_w-34,window_y+8,18,18,MINIMIZE_BG);
    gop_draw_text_sized_at(window_x+window_w-54,window_y+12,"x",TITLE_FG,CLOSE_BG,10);
    gop_draw_text_sized_at(window_x+window_w-30,window_y+12,"-",TITLE_FG,MINIMIZE_BG,10);

    text_x=window_x+16;
    text_y=window_y+TITLE_HEIGHT+14;
    text_w=window_w-32;
    text_h=window_h-TITLE_HEIGHT-28;
    gop_draw_rect(text_x,text_y,text_w,text_h,TERM_BG);
    cursor_x=text_x;
    cursor_y=text_y;
}

void terminal_init(uint32_t screen_width, uint32_t screen_height){
    window_w=screen_width>WINDOW_MAX_W+40 ? WINDOW_MAX_W : screen_width-40;
    window_h=screen_height>WINDOW_TOP+WINDOW_MAX_H+30
        ? WINDOW_MAX_H : screen_height-WINDOW_TOP-30;
    if(window_w<320) window_w=320;
    if(window_h<240) window_h=240;
    window_x=(screen_width-window_w)/2;
    window_y=WINDOW_TOP;
    input_length=0;
    draw_window();

    terminal_write("PureC Terminal 0.2.0\n");
    terminal_write("Standalone terminal module integrated into userspace.\n");
    terminal_write("Type 'help' for commands or 'dmesg' for the boot log.\n\n");
    terminal_prompt();
}

void terminal_handle_key(char c){
    if(c=='\n' || c=='\r'){
        terminal_putc('\n');
        input_buffer[input_length]=0;
        commands_execute(input_buffer);
        input_length=0;
        terminal_prompt();
        return;
    }
    if(c=='\b' || c==127){
        if(input_length){
            input_length--;
            terminal_putc('\b');
        }
        return;
    }
    if(c<' ' || c>'~') return;
    if(input_length<SHELL_BUFFER_SIZE-1){
        input_buffer[input_length++]=c;
        terminal_putc(c);
    }
}

void terminal_putc(char c){ putc_colored(c,TERM_FG); }

void terminal_write(const char *text){
    if(!text) return;
    while(*text) terminal_putc(*text++);
}

void terminal_write_colored(const char *text, uint32_t color){
    if(!text) return;
    while(*text) putc_colored(*text++,color);
}

void terminal_printf(const char *format, ...){
    if(!format) return;
    va_list args;
    va_start(args,format);
    while(*format){
        if(*format!='%'){
            terminal_putc(*format++);
            continue;
        }
        format++;
        if(*format=='%') terminal_putc('%');
        else if(*format=='c') terminal_putc((char)va_arg(args,int));
        else if(*format=='s'){
            const char *text=va_arg(args,const char*);
            terminal_write(text ? text : "(null)");
        } else if(*format=='d' || *format=='i') write_signed((int64_t)va_arg(args,int));
        else if(*format=='u') write_unsigned((uint64_t)va_arg(args,unsigned int),10,false);
        else if(*format=='x') write_unsigned((uint64_t)va_arg(args,unsigned int),16,false);
        else if(*format=='X') write_unsigned((uint64_t)va_arg(args,unsigned int),16,true);
        else {
            terminal_putc('%');
            if(*format) terminal_putc(*format);
        }
        if(*format) format++;
    }
    va_end(args);
}

void terminal_clear(void){
    gop_draw_rect(text_x,text_y,text_w,text_h,TERM_BG);
    cursor_x=text_x;
    cursor_y=text_y;
}

void terminal_prompt(void){ terminal_write_colored(PROMPT_TEXT,PROMPT_FG); }

uint32_t terminal_get_window_width(void){ return window_w; }
uint32_t terminal_get_window_height(void){ return window_h; }
