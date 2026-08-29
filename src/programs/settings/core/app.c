#include "settings/app.h"
#include "settings/audio_page.h"
#include "settings/wifi_page.h"
#include "../../../libgui/include/pguiw.h"
void settings_app_init(struct settings_app *app) {
    settings_model_init(&app->model);
    settings_model_load(&app->model);
    settings_model_apply(&app->model);
    app->tab = 0;
}
void settings_app_draw(struct settings_app *app, struct pg_window *w, const struct pg_event *ev) {
    pg_window_begin(w);
    pg_label(w, 16, 16, "Settings");
    bool s0 = app->tab == 0;
    bool s1 = app->tab == 1;
    if (pg_button(w, (struct pg_rect){16, 36, 90, 28}, s0 ? "[ Sound ]" : "Sound", ev) && ev->type == PG_EVENT_MOUSE_DOWN) app->tab = 0;
    if (pg_button(w, (struct pg_rect){112, 36, 90, 28}, s1 ? "[ Wi-Fi ]" : "Wi-Fi", ev) && ev->type == PG_EVENT_MOUSE_DOWN) app->tab = 1;
    if (app->tab == 0) audio_page_draw(w, &app->model, ev);
    else wifi_page_draw(w, ev);
    pg_window_end(w);
}
