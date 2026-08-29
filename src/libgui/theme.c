#include "include/puregui.h"

struct pg_theme pg_theme_default(void){
    struct pg_theme theme={
        .desktop=0x181825,
        .window=0x1E1E2E,
        .titlebar=0x313244,
        .border=0x45475A,
        .text=0xCDD6F4,
        .muted_text=0x9399B2,
        .accent=0x89B4FA,
        .danger=0xF38BA8,
        .shadow=0x11111B
    };
    return theme;
}
