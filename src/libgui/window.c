#include "internal.h"
#include "../libc/include/purec.h"

#define PG_MINIMUM_WIDTH 160
#define PG_MINIMUM_HEIGHT 96

static void update_client_rect(struct pg_window *window){
    window->client.x=window->frame.x+PG_WINDOW_BORDER;
    window->client.y=window->frame.y+PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER;
    window->client.width=window->frame.width-PG_WINDOW_BORDER*2;
    window->client.height=window->frame.height-PG_TITLEBAR_HEIGHT
        -PG_WINDOW_BORDER*2;
}

bool pg_window_init(struct pg_window *window, const char *title,
                    uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height){
    if(!window || width<PG_MINIMUM_WIDTH || height<PG_MINIMUM_HEIGHT)
        return false;
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available
       || width>display.width || height>display.height) return false;
    if(x>display.width-width) x=display.width-width;
    if(y>display.height-height) y=display.height-height;
    window->frame=(struct pg_rect){x,y,width,height};
    window->theme=pg_theme_default();
    pc_copy(window->title,title ? title : "PureGUI",sizeof(window->title));
    window->previous_mouse_x=-1;
    window->previous_mouse_y=-1;
    window->previous_mouse_buttons=0;
    window->open=true;
    update_client_rect(window);
    return true;
}

bool pg_window_center(struct pg_window *window, const char *title,
                      uint32_t width, uint32_t height){
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || width>display.width
       || height>display.height) return false;
    return pg_window_init(window,title,(display.width-width)/2,
                          (display.height-height)/2,width,height);
}

void pg_window_begin(struct pg_window *window){
    if(!window || !window->open) return;
    pc_display_begin_update();
    pc_draw_rect(window->frame.x+6,window->frame.y+6,
                 window->frame.width,window->frame.height,
                 window->theme.shadow);
    pc_draw_rect(window->frame.x,window->frame.y,
                 window->frame.width,window->frame.height,
                 window->theme.border);
    pc_draw_rect(window->frame.x+PG_WINDOW_BORDER,
                 window->frame.y+PG_WINDOW_BORDER,
                 window->frame.width-PG_WINDOW_BORDER*2,
                 PG_TITLEBAR_HEIGHT,window->theme.titlebar);
    pc_draw_rect(window->client.x,window->client.y,
                 window->client.width,window->client.height,
                 window->theme.window);
    pg_internal_draw_text_clipped(window->frame.x+10,window->frame.y+10,
                                  window->title,window->theme.text,
                                  window->theme.titlebar,&window->frame);
    pc_draw_rect(window->frame.x+window->frame.width-24,
                 window->frame.y+8,14,14,window->theme.danger);
    pc_draw_text(window->frame.x+window->frame.width-21,
                 window->frame.y+11,"x",window->theme.window,
                 window->theme.danger);
}

void pg_window_end(struct pg_window *window){
    if(window && window->open) pc_display_end_update();
}

void pg_window_close(struct pg_window *window){
    if(window) window->open=false;
}

bool pg_window_is_open(const struct pg_window *window){
    return window && window->open;
}

struct pg_rect pg_window_client(const struct pg_window *window){
    return window ? window->client : (struct pg_rect){0,0,0,0};
}

void pg_window_clear(struct pg_window *window, uint32_t color){
    if(window && window->open)
        pc_draw_rect(window->client.x,window->client.y,
                     window->client.width,window->client.height,color);
}
