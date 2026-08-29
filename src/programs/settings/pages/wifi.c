#include "settings/wifi_page.h"
#include "../../../libgui/include/pguiw.h"
void wifi_page_draw(struct pg_window *w, const struct pg_event *ev) {
    (void)ev;
    pg_label(w, 16, 80, "Wi-Fi (coming soon)");
    pg_label(w, 16, 104, "No hardware support yet");
    pg_panel(w, (struct pg_rect){16, 128, w->client.width - 32, 40});
    pg_window_text(w, w->client.x + 28, w->client.y + 142, "Scan placeholder", w->theme.muted_text);
}
