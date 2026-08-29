#include "settings/audio_page.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
static void format_volume(char *out, int v, int muted) {
    char vs[8];
    if (v >= 100) { vs[0] = '0' + v / 100; vs[1] = '0' + (v / 10) % 10; vs[2] = '0' + v % 10; vs[3] = '\0'; }
    else if (v >= 10) { vs[0] = '0' + v / 10; vs[1] = '0' + v % 10; vs[2] = '\0'; }
    else { vs[0] = '0' + v; vs[1] = '\0'; }
    pc_copy(out, "Volume: ", 48);
    uint32_t len = pc_strlen(out);
    for (int i = 0; vs[i]; i++) out[len++] = vs[i];
    if (muted) { out[len++] = ' '; out[len++] = '('; out[len++] = 'm'; out[len++] = 'u'; out[len++] = 't'; out[len++] = 'e'; out[len++] = 'd'; out[len++] = ')'; }
    out[len] = '\0';
}
void audio_page_draw(struct pg_window *w, struct settings_model *m, const struct pg_event *ev) {
    struct audio_status st = {0};
    pc_audio_get_status(&st);
    char line[48];
    format_volume(line, m->volume, m->muted);
    pg_label(w, 16, 80, line);
    if (pg_button(w, (struct pg_rect){16, 104, 60, 28}, "-", ev) && ev->type == PG_EVENT_MOUSE_DOWN) {
        m->volume = m->volume >= 5 ? m->volume - 5 : 0;
        settings_model_apply(m);
        settings_model_save(m);
    }
    if (pg_button(w, (struct pg_rect){84, 104, 60, 28}, "+", ev) && ev->type == PG_EVENT_MOUSE_DOWN) {
        m->volume = m->volume + 5 > 100 ? 100 : m->volume + 5;
        settings_model_apply(m);
        settings_model_save(m);
    }
    if (pg_button(w, (struct pg_rect){152, 104, 80, 28}, m->muted ? "Unmute" : "Mute", ev) && ev->type == PG_EVENT_MOUSE_DOWN) {
        m->muted ^= 1;
        settings_model_apply(m);
        settings_model_save(m);
    }
    if (pg_button(w, (struct pg_rect){240, 104, 80, 28}, "Test", ev) && ev->type == PG_EVENT_MOUSE_DOWN) {
        pc_audio_play_test();
    }
    char dev[48] = "Device: ";
    char ds[8]; ds[0] = '0' + (st.selected_output_device % 10); ds[1] = '\0';
    char dc[8]; dc[0] = '0' + (st.output_device_count % 10); dc[1] = '\0';
    uint32_t dl = pc_strlen(dev);
    dev[dl++] = ds[0]; dev[dl++] = '/'; dev[dl++] = dc[0]; dev[dl] = '\0';
    pg_label(w, 16, 144, dev);
    if (pg_button(w, (struct pg_rect){16, 164, 90, 28}, "Prev", ev) && ev->type == PG_EVENT_MOUSE_DOWN) {
        uint32_t c = st.output_device_count ? st.output_device_count : 1;
        m->device = (int)((st.selected_output_device + c - 1) % c);
        pc_audio_select_output((uint32_t)m->device);
        settings_model_save(m);
    }
    if (pg_button(w, (struct pg_rect){112, 164, 90, 28}, "Next", ev) && ev->type == PG_EVENT_MOUSE_DOWN) {
        uint32_t c = st.output_device_count ? st.output_device_count : 1;
        m->device = (int)((st.selected_output_device + 1) % c);
        pc_audio_select_output((uint32_t)m->device);
        settings_model_save(m);
    }
}
