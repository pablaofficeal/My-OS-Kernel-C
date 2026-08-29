#include "../../libgui/include/puregui.h"
#include "../../libc/include/purec.h"
#include "settings/app.h"
static int settings_main(void) {
    struct settings_app app;
    settings_app_init(&app);
    struct pg_window w;
    if (!pg_window_center(&w, "Settings", 520, 300)) return 1;
    struct pg_event ev = {0};
    settings_app_draw(&app, &w, &ev);
    while (pg_window_is_open(&w)) {
        if (!pg_window_poll_event(&w, &ev)) { pc_sleep(16); continue; }
        if (ev.type == PG_EVENT_CLOSE) break;
        settings_app_draw(&app, &w, &ev);
    }
    return 0;
}
void _start(void) { pc_exit(settings_main()); }
