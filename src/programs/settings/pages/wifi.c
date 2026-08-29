#include "settings/wifi_page.h"
#include "../../../libgui/include/pguiw.h"
#define SIDEBAR_WIDTH 150
#define TOOLBAR_HEIGHT 46
void wifi_page_draw(struct pg_window *w, const struct pg_event *ev){
    (void)ev;
    uint32_t left=SIDEBAR_WIDTH+14; uint32_t top=TOOLBAR_HEIGHT+12;
    uint32_t width=w->client.width-left-14;
    pg_window_rect(w,(struct pg_rect){left,top,width,72},0x2B2D40);
    pg_window_text(w,left+14,top+14,"Wi-Fi",w->theme.text);
    pg_window_text(w,left+14,top+34,"Coming soon  \xE2\x80\xA2  hardware not yet available",w->theme.muted_text);
    pg_window_rect(w,(struct pg_rect){left,top+86,width,88},0x2B2D40);
    pg_window_text(w,left+14,top+100,"No adapter detected",w->theme.text);
    pg_window_text(w,left+14,top+120,"Scanning and connection will appear here",w->theme.muted_text);
    pg_window_text(w,left+14,top+140,"when the driver is ready.",w->theme.muted_text);
    pg_button(w,(struct pg_rect){left+14,top+160,96,22},"Scan",ev);
}
