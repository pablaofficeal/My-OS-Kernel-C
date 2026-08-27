#include "userspace.h"
#include "terminal/terminal.h"
#include "monitor/monitor.h"
#include "../drivers/gop.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/mouse/usb_mouse.h"
#include "../drivers/timer.h"
#include "../kernel/klog.h"
#include "../kernel/boot_diag.h"
#include "../kernel/panic.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stdbool.h>

#define DESKTOP_BG       0x181825
#define TOPBAR_BG        0x313244
#define TOPBAR_FG        0xCDD6F4
#define TOPBAR_MUTED     0x9399B2
#define TOPBAR_ACCENT    0x89B4FA
#define TOPBAR_HEIGHT    28
#define MOUSE_DEBUG_X    12
#define MOUSE_DEBUG_Y    38
#define MOUSE_DEBUG_W    380
#define MOUSE_DEBUG_H    84
#define ICON_Y           48
#define ICON_W           58
#define ICON_H           72

static uint32_t desktop_width;
static uint32_t desktop_height;
static uint32_t htop_icon_x=420;
static uint32_t terminal_icon_x=500;
static uint8_t previous_mouse_buttons;

static bool point_inside(int32_t x, int32_t y, uint32_t left, uint32_t top,
                         uint32_t width, uint32_t height){
    return x>=(int32_t)left && y>=(int32_t)top
        && x<(int32_t)(left+width) && y<(int32_t)(top+height);
}

static void draw_htop_icon(void){
    gop_draw_rect(htop_icon_x,ICON_Y,ICON_W,50,0x313244);
    gop_draw_rect(htop_icon_x+7,ICON_Y+8,44,30,0x1E1E2E);
    gop_draw_line(htop_icon_x+11,ICON_Y+31,htop_icon_x+19,ICON_Y+21,0x89B4FA);
    gop_draw_line(htop_icon_x+19,ICON_Y+21,htop_icon_x+28,ICON_Y+27,0x89B4FA);
    gop_draw_line(htop_icon_x+28,ICON_Y+27,htop_icon_x+39,ICON_Y+14,0xA6E3A1);
    gop_draw_line(htop_icon_x+39,ICON_Y+14,htop_icon_x+47,ICON_Y+19,0xA6E3A1);
    gop_draw_text_at(htop_icon_x+9,ICON_Y+55,"HTOP",TOPBAR_FG,DESKTOP_BG);
}

static void draw_terminal_icon(void){
    gop_draw_rect(terminal_icon_x,ICON_Y,ICON_W,50,0x313244);
    gop_draw_rect(terminal_icon_x+7,ICON_Y+8,44,30,0x1E1E2E);
    gop_draw_text_at(terminal_icon_x+13,ICON_Y+18,">_",0xA6E3A1,0x1E1E2E);
    gop_draw_text_at(terminal_icon_x,ICON_Y+55,"Terminal",TOPBAR_FG,DESKTOP_BG);
}

static void draw_desktop(void){
    desktop_width=gop_get_width();
    desktop_height=gop_get_height();
    if(desktop_width==0) desktop_width=1280;
    if(desktop_height==0) desktop_height=800;
    htop_icon_x=desktop_width>560 ? 420 : desktop_width-140;
    terminal_icon_x=htop_icon_x+72;

    gop_clear(DESKTOP_BG);
    gop_draw_rect(0,0,desktop_width,TOPBAR_HEIGHT,TOPBAR_BG);
    gop_draw_text_at(12,8,"PureC OS",TOPBAR_ACCENT,TOPBAR_BG);
    gop_draw_text_at(120,8,"Userspace 0.2.0",TOPBAR_FG,TOPBAR_BG);

    const char *status="desktop: icons  |  drag title bars  |  help";
    uint32_t status_width=(uint32_t)strlen(status)*8;
    uint32_t status_x=desktop_width>status_width+12
        ? desktop_width-status_width-12 : 220;
    gop_draw_text_at(status_x,8,status,TOPBAR_MUTED,TOPBAR_BG);
    draw_htop_icon();
    draw_terminal_icon();
}

static void redraw_scene(void){
    mouse_begin_framebuffer_update();
    draw_desktop();
    if(terminal_is_visible()) terminal_redraw();
    if(monitor_window_is_visible()) monitor_window_draw();
    mouse_end_framebuffer_update();
}

static void handle_desktop_mouse(void){
    struct mouse_state mouse=mouse_get_state();
    bool pressed=(mouse.buttons&1) && !(previous_mouse_buttons&1);
    bool released=!(mouse.buttons&1) && (previous_mouse_buttons&1);
    bool redraw=false;
    bool consumed=false;

    if(monitor_window_is_visible()){
        consumed=monitor_window_contains_point(mouse.x,mouse.y);
        redraw=monitor_window_handle_mouse(mouse.x,mouse.y,mouse.buttons,
                                            pressed,released,
                                            desktop_width,desktop_height);
    }
    if(!consumed && terminal_is_visible()){
        consumed=terminal_contains_point(mouse.x,mouse.y);
        redraw=terminal_handle_mouse(mouse.x,mouse.y,mouse.buttons,
                                      pressed,released,
                                      desktop_width,desktop_height) || redraw;
    }
    if(pressed && !consumed
       && point_inside(mouse.x,mouse.y,htop_icon_x,ICON_Y,ICON_W,ICON_H)){
        monitor_run();
        redraw=true;
    } else if(pressed && !consumed
              && point_inside(mouse.x,mouse.y,terminal_icon_x,ICON_Y,
                              ICON_W,ICON_H)){
        terminal_set_visible(true);
        redraw=true;
    }
    previous_mouse_buttons=mouse.buttons;
    if(redraw) redraw_scene();
}

uint32_t userspace_get_width(void){ return desktop_width; }
uint32_t userspace_get_height(void){ return desktop_height; }

void userspace_set_mouse_debug(bool enabled){
    mouse_set_debug_overlay(enabled);
    if(!enabled){
        gop_draw_rect(MOUSE_DEBUG_X,MOUSE_DEBUG_Y,MOUSE_DEBUG_W,MOUSE_DEBUG_H,DESKTOP_BG);
    }
    mouse_redraw();
}

void userspace_init(void){
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: validating framebuffer");
    if(!gop_is_available()) kernel_panic("userspace requires an active framebuffer");
    if(gop_get_width() < 320 || gop_get_height() < 240)
        kernel_panic("framebuffer is too small for userspace");

    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: initializing keyboard");
    keyboard_init();
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: drawing desktop");
    // draw_desktop clears the boot log; subsequent diagnostics remain in the
    // ring and serial, while panic forcibly restores a visible panic screen.
    klog_set_screen_enabled(false);
    draw_desktop();
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: initializing terminal");
    terminal_init(desktop_width,desktop_height);
    terminal_set_visible(true);

    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: configuring mouse bounds");
    mouse_set_bounds((int32_t)desktop_width,(int32_t)desktop_height);
    userspace_set_mouse_debug(true);
    klog_set_screen_enabled(false);
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: initialization complete");
}

void userspace_run(void){
    for(;;){
        ps2_mouse_poll();
        usb_mouse_poll();
        keyboard_poll();
        handle_desktop_mouse();

        char c;
        while(keyboard_try_getc(&c)){
            if(terminal_is_visible()) terminal_handle_key(c);
        }
        monitor_window_update();

        __asm__ volatile("pause");
        for(volatile uint32_t wait=0;wait<10000;wait++){
            __asm__ volatile("nop");
        }
    }
}
