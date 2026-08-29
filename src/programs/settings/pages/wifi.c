#include "settings/wifi_page.h"
#include "../../../libgui/include/pguiw.h"

#define PAGE_LEFT 198
#define PAGE_TOP 80

void wifi_page_draw(struct pg_window *window,const struct pg_event *event){
    uint32_t width=window->client.width-PAGE_LEFT-22;
    pg_window_text(window,PAGE_LEFT,PAGE_TOP-2,"Network",window->theme.text);
    pg_window_rect(window,(struct pg_rect){PAGE_LEFT,PAGE_TOP+24,width,140},
                   0x2B2D40);
    pg_window_text(window,PAGE_LEFT+18,PAGE_TOP+46,
                   "VirtualBox network target",window->theme.text);
    pg_window_text(window,PAGE_LEFT+18,PAGE_TOP+70,
                   "Intel PRO/1000 MT Desktop (82540EM)",
                   window->theme.accent);
    pg_window_text(window,PAGE_LEFT+18,PAGE_TOP+94,
                   "VirtualBox presents Ethernet, even when the host uses Wi-Fi.",
                   window->theme.muted_text);
    pg_window_text(window,PAGE_LEFT+18,PAGE_TOP+114,
                   "Next: e1000 RX/TX rings, then ARP, IPv4, UDP and DHCP.",
                   window->theme.muted_text);
    pg_button(window,(struct pg_rect){PAGE_LEFT+18,PAGE_TOP+132,128,24},
              "Driver pending",event);
}
