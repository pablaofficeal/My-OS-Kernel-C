#include "desktop_apps.h"
#include "calculator_app.h"
#include "calendar_app.h"
#include "clock_app.h"
#include "datetime_service.h"
#include "../../drivers/gop.h"
#include "../../drivers/mouse/ps2_mouse.h"

#define WINDOW_WIDTH 360
#define WINDOW_HEIGHT 270
#define TITLE_BAR_HEIGHT 30

static enum desktop_app active_app;
static bool visible;
static bool dragging;
static uint32_t window_x = 180;
static uint32_t window_y = 90;
static int32_t drag_offset_x;
static int32_t drag_offset_y;

static bool point_inside(
    int32_t point_x,
    int32_t point_y,
    uint32_t left,
    uint32_t top,
    uint32_t width,
    uint32_t height
) {
    return point_x >= (int32_t)left
        && point_y >= (int32_t)top
        && point_x < (int32_t)(left + width)
        && point_y < (int32_t)(top + height);
}

static const char *active_app_title(void) {
    switch (active_app) {
        case DESKTOP_APP_CLOCK:
            return "Clock";
        case DESKTOP_APP_CALCULATOR:
            return "Calculator";
        case DESKTOP_APP_CALENDAR:
            return "Calendar";
    }

    return "Application";
}

static void draw_window_frame(void) {
    gop_draw_rect(
        window_x + 5,
        window_y + 5,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0x11111B
    );
    gop_draw_rect(
        window_x,
        window_y,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0x45475A
    );
    gop_draw_rect(
        window_x + 1,
        window_y + 1,
        WINDOW_WIDTH - 2,
        WINDOW_HEIGHT - 2,
        0x1E1E2E
    );
    gop_draw_rect(
        window_x + 1,
        window_y + 1,
        WINDOW_WIDTH - 2,
        TITLE_BAR_HEIGHT,
        0x89B4FA
    );
    gop_draw_text_at(
        window_x + 12,
        window_y + 10,
        active_app_title(),
        0x1E1E2E,
        0x89B4FA
    );
    gop_draw_rect(
        window_x + WINDOW_WIDTH - 27,
        window_y + 6,
        18,
        18,
        0xF38BA8
    );
    gop_draw_text_at(
        window_x + WINDOW_WIDTH - 23,
        window_y + 10,
        "x",
        0x1E1E2E,
        0xF38BA8
    );
}

static void draw_active_app(void) {
    switch (active_app) {
        case DESKTOP_APP_CLOCK:
            clock_app_draw(window_x, window_y);
            break;
        case DESKTOP_APP_CALCULATOR:
            calculator_app_draw(window_x, window_y);
            break;
        case DESKTOP_APP_CALENDAR:
            calendar_app_draw(window_x, window_y);
            break;
    }
}

static void open_active_app(void) {
    switch (active_app) {
        case DESKTOP_APP_CLOCK:
            clock_app_open();
            break;
        case DESKTOP_APP_CALCULATOR:
            calculator_app_open();
            break;
        case DESKTOP_APP_CALENDAR:
            calendar_app_open();
            break;
    }
}

static bool handle_active_app_key(char key) {
    switch (active_app) {
        case DESKTOP_APP_CLOCK:
            return clock_app_handle_key(key);
        case DESKTOP_APP_CALCULATOR:
            return calculator_app_handle_key(key);
        case DESKTOP_APP_CALENDAR:
            return calendar_app_handle_key(key);
    }

    return false;
}

void desktop_apps_init(void) {
    datetime_service_init();
}

void desktop_apps_save_time(void) {
    datetime_service_save();
}

void desktop_apps_draw(void) {
    if (!visible) {
        return;
    }

    mouse_begin_framebuffer_update();
    draw_window_frame();
    draw_active_app();
    mouse_end_framebuffer_update();
}

void desktop_apps_open(
    enum desktop_app app,
    uint32_t screen_width,
    uint32_t screen_height
) {
    active_app = app;
    visible = true;
    dragging = false;

    if (window_x + WINDOW_WIDTH > screen_width) {
        window_x = 10;
    }
    if (window_y + WINDOW_HEIGHT > screen_height) {
        window_y = 34;
    }

    open_active_app();
    desktop_apps_draw();
}

bool desktop_apps_is_visible(void) {
    return visible;
}

bool desktop_apps_contains_point(int32_t point_x, int32_t point_y) {
    return visible && point_inside(
        point_x,
        point_y,
        window_x,
        window_y,
        WINDOW_WIDTH,
        WINDOW_HEIGHT
    );
}

bool desktop_apps_handle_mouse(
    int32_t point_x,
    int32_t point_y,
    uint8_t buttons,
    bool pressed,
    bool released,
    uint32_t screen_width,
    uint32_t screen_height,
    bool *redraw_required
) {
    bool captured;

    if (redraw_required != 0) {
        *redraw_required = false;
    }
    if (!visible) {
        return false;
    }

    captured = dragging || desktop_apps_contains_point(point_x, point_y);

    if (pressed && point_inside(
            point_x,
            point_y,
            window_x + WINDOW_WIDTH - 27,
            window_y + 6,
            18,
            18)) {
        visible = false;
        dragging = false;
        if (redraw_required != 0) {
            *redraw_required = true;
        }
        return true;
    }

    if (pressed && point_inside(
            point_x,
            point_y,
            window_x,
            window_y,
            WINDOW_WIDTH,
            TITLE_BAR_HEIGHT)) {
        dragging = true;
        drag_offset_x = point_x - (int32_t)window_x;
        drag_offset_y = point_y - (int32_t)window_y;
    }

    if (dragging && (buttons & 1) != 0) {
        int32_t next_x = point_x - drag_offset_x;
        int32_t next_y = point_y - drag_offset_y;
        int32_t maximum_x = screen_width > WINDOW_WIDTH
            ? (int32_t)(screen_width - WINDOW_WIDTH)
            : 0;
        int32_t maximum_y = screen_height > WINDOW_HEIGHT
            ? (int32_t)(screen_height - WINDOW_HEIGHT)
            : 28;

        if (maximum_y < 28) {
            maximum_y = 28;
        }
        if (next_x < 0) {
            next_x = 0;
        }
        if (next_y < 28) {
            next_y = 28;
        }
        if (next_x > maximum_x) {
            next_x = maximum_x;
        }
        if (next_y > maximum_y) {
            next_y = maximum_y;
        }

        if ((uint32_t)next_x == window_x && (uint32_t)next_y == window_y) {
            return true;
        }

        window_x = (uint32_t)next_x;
        window_y = (uint32_t)next_y;
        if (redraw_required != 0) {
            *redraw_required = true;
        }
        return true;
    }

    if (released && dragging) {
        dragging = false;
    }

    return captured;
}

bool desktop_apps_handle_key(char key) {
    if (!visible || !handle_active_app_key(key)) {
        return false;
    }

    desktop_apps_draw();
    return true;
}

void desktop_apps_update(void) {
    bool time_changed = datetime_service_update();

    if (!time_changed || !visible) {
        return;
    }
    if (active_app == DESKTOP_APP_CLOCK
        || active_app == DESKTOP_APP_CALENDAR) {
        desktop_apps_draw();
    }
}
