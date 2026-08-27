#include "userspace.h"
#include "terminal/terminal.h"
#include "../drivers/gop.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/mouse/usb_mouse.h"
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

static uint32_t desktop_width;
static uint32_t desktop_height;

static void draw_desktop(void){
    desktop_width=gop_get_width();
    desktop_height=gop_get_height();
    if(desktop_width==0) desktop_width=1280;
    if(desktop_height==0) desktop_height=800;

    gop_clear(DESKTOP_BG);
    gop_draw_rect(0,0,desktop_width,TOPBAR_HEIGHT,TOPBAR_BG);
    gop_draw_text_at(12,8,"PureC OS",TOPBAR_ACCENT,TOPBAR_BG);
    gop_draw_text_at(120,8,"Userspace 0.2.0",TOPBAR_FG,TOPBAR_BG);

    const char *status="terminal: module  |  help  dmesg  mouse  debug";
    uint32_t status_width=(uint32_t)strlen(status)*8;
    uint32_t status_x=desktop_width>status_width+12
        ? desktop_width-status_width-12 : 220;
    gop_draw_text_at(status_x,8,status,TOPBAR_MUTED,TOPBAR_BG);
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

        char c;
        while(keyboard_try_getc(&c)) terminal_handle_key(c);

        __asm__ volatile("pause");
        for(volatile uint32_t i=0;i<10000;i++) __asm__ volatile("nop");
    }
}
