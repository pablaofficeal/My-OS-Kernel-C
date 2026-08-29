#include "settings/wifi_page.h"
#include "../../../libgui/include/pguiw.h"
#define SIDEBAR_WIDTH 150
#define TOOLBAR_HEIGHT 46
void wifi_page_draw(struct pg_window *w, const struct pg_event *ev){
    (void)ev;
    uint32_t left=SIDEBAR_WIDTH+14;
    uint32_t top=TOOLBAR_HEIGHT+12;
    uint32_t width=w->client.width-left-14;
    pg_window_rect(w,(struct pg_rect){left,top,width,68},0x2B2D40);
    pg_window_text(w,left+14,top+12,"Wi-Fi",w->theme.text);
    pg_window_text(w,left+14,top+32,"Coming soon  \xE2\x80\xA2  hardware not yet available",w->theme.muted_text);
    pg_window_rect(w,(struct pg_rect){left,top+82,width,88},0x2B2D40);
    pg_window_text(w,left+14,top+96,"No adapter detected",w->theme.text);
    pg_window_text(w,left+14,top+116,"Scanning and connection will appear here",w->theme.muted_text);
    pg_window_text(w,left+14,top+132,"when the driver is ready.",w->theme.muted_text);
    pg_button(w,(struct pg_rect){left+14,top+152,96,22},"Scan",ev);
}
