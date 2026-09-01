#include "window.h"
#include "../../libc/include/purec.h"

#define HEXEDIT_MAX_WIDTH 960
#define HEXEDIT_MAX_HEIGHT 640
#define HEXEDIT_MIN_WIDTH 640
#define HEXEDIT_MIN_HEIGHT 400
#define HEXEDIT_SCREEN_MARGIN 32

bool hexedit_window_init(struct hexedit_window *window){
    if(!window) return false;
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available) return false;
    uint32_t width = display.width>HEXEDIT_SCREEN_MARGIN*2 ? display.width-HEXEDIT_SCREEN_MARGIN*2 : display.width;
    uint32_t height = display.height>HEXEDIT_SCREEN_MARGIN*2 ? display.height-HEXEDIT_SCREEN_MARGIN*2 : display.height;
    if(width>HEXEDIT_MAX_WIDTH) width=HEXEDIT_MAX_WIDTH;
    if(height>HEXEDIT_MAX_HEIGHT) height=HEXEDIT_MAX_HEIGHT;
    if(width<HEXEDIT_MIN_WIDTH) width=HEXEDIT_MIN_WIDTH;
    if(height<HEXEDIT_MIN_HEIGHT) height=HEXEDIT_MIN_HEIGHT;
    window->render_active=false;
    return pg_window_center(&window->gui,"PureC HexEdit",width,height);
}

void hexedit_window_begin(struct hexedit_window *window){
    if(!window || !pg_window_is_open(&window->gui) || window->render_active) return;
    pg_window_begin(&window->gui);
    window->render_active=true;
}

void hexedit_window_end(struct hexedit_window *window){
    if(!window || !window->render_active) return;
    window->render_active=false;
    pg_window_end(&window->gui);
}

bool hexedit_window_poll_event(struct hexedit_window *window, struct pg_event *event){
    return window && pg_window_poll_event(&window->gui,event);
}

bool hexedit_window_is_minimized(const struct hexedit_window *window){
    return window && pg_window_is_minimized(&window->gui);
}

void hexedit_window_shutdown(struct hexedit_window *window){
    if(!window) return;
    if(window->render_active) hexedit_window_end(window);
    if(pg_window_is_open(&window->gui)) pg_window_close(&window->gui);
}

struct pg_rect hexedit_window_client(const struct hexedit_window *window){
    if(!window) return (struct pg_rect){0,0,0,0};
    return pg_window_client(&window->gui);
}
