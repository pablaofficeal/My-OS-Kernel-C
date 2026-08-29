#include "internal.h"
#include "../libc/include/purec.h"

static uint8_t changed_button(uint8_t changed){
    if(changed&1) return 1;
    if(changed&2) return 2;
    if(changed&4) return 3;
    return 0;
}

bool pg_window_poll_event(struct pg_window *window, struct pg_event *event){
    if(!window || !event || !window->open) return false;
    *event=(struct pg_event){.type=PG_EVENT_NONE};
    int32_t key=pc_try_getchar();
    if(key>=0){
        event->key=key;
        event->type=key==27 ? PG_EVENT_CLOSE : PG_EVENT_KEY;
        if(event->type==PG_EVENT_CLOSE) window->open=false;
        return true;
    }
    struct mouse_state mouse;
    if(!pc_mouse_get(&mouse)) return false;
    event->x=mouse.x;
    event->y=mouse.y;
    uint8_t changed=mouse.buttons^window->previous_mouse_buttons;
    if(changed){
        event->button=changed_button(changed);
        event->type=(mouse.buttons&changed) ? PG_EVENT_MOUSE_DOWN
                                           : PG_EVENT_MOUSE_UP;
    } else if(mouse.x!=window->previous_mouse_x
              || mouse.y!=window->previous_mouse_y){
        event->type=PG_EVENT_MOUSE_MOVE;
    }
    window->previous_mouse_x=mouse.x;
    window->previous_mouse_y=mouse.y;
    window->previous_mouse_buttons=mouse.buttons;
    if(event->type==PG_EVENT_MOUSE_UP && event->button==1){
        struct pg_rect close={
            window->frame.x+window->frame.width-24,
            window->frame.y+8,14,14
        };
        if(pg_internal_point_inside(mouse.x,mouse.y,&close)){
            event->type=PG_EVENT_CLOSE;
            window->open=false;
            return true;
        }
        struct pg_rect minimize={
            window->frame.x+window->frame.width-44,
            window->frame.y+8,14,14
        };
        if(pg_internal_point_inside(mouse.x,mouse.y,&minimize)){
            window->minimized=!window->minimized;
            event->type=PG_EVENT_MINIMIZE;
        }
    }
    return event->type!=PG_EVENT_NONE;
}
