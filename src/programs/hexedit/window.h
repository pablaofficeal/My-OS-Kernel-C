#pragma once
#include <stdbool.h>
#include "../../libgui/include/puregui.h"

struct hexedit_window {
    struct pg_window gui;
    bool render_active;
};

bool hexedit_window_init(struct hexedit_window *window);
void hexedit_window_begin(struct hexedit_window *window);
void hexedit_window_end(struct hexedit_window *window);
bool hexedit_window_poll_event(struct hexedit_window *window, struct pg_event *event);
bool hexedit_window_is_minimized(const struct hexedit_window *window);
void hexedit_window_shutdown(struct hexedit_window *window);
struct pg_rect hexedit_window_client(const struct hexedit_window *window);
