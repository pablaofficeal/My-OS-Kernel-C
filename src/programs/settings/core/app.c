#include "settings/app.h"
#include "settings/audio_page.h"
#include "settings/wifi_page.h"
#include "../../../libgui/include/pguiw.h"
#define SIDEBAR_WIDTH 150
#define TOOLBAR_HEIGHT 46
#define STATUS_HEIGHT 30
static bool clicked(const struct pg_window *w, struct pg_rect b, const struct pg_event *e){
    if(!w||!e||e->type!=PG_EVENT_MOUSE_UP||e->button!=1) return false;
    int32_t x=e->x-(int32_t)w->client.x; int32_t y=e->y-(int32_t)w->client.y;
    return x>=(int32_t)b.x && y>=(int32_t)b.y && x<(int32_t)(b.x+b.width) && y<(int32_t)(b.y+b.height);
}
void settings_app_init(struct settings_app *app){
    settings_model_init(&app->model);
    settings_model_load(&app->model);
    settings_model_apply(&app->model);
    app->tab=0;
}
void settings_app_draw(struct settings_app *app, struct pg_window *w, const struct pg_event *ev){
    pg_window_begin(w);
    if(pg_window_is_minimized(w)){ pg_window_end(w); return; }
    pg_window_clear(w,0x1E1E2E);
    uint32_t cw=w->client.width;
    uint32_t ch=w->client.height;
    pg_window_rect(w,(struct pg_rect){0,0,cw,TOOLBAR_HEIGHT},0x252638);
    pg_window_text(w,14,13,"Settings",w->theme.text);
    pg_window_text(w,14,28,"/config/settings.ini",0x7F849C);
    uint32_t sidebar_h=ch-TOOLBAR_HEIGHT-STATUS_HEIGHT;
    pg_window_rect(w,(struct pg_rect){0,TOOLBAR_HEIGHT,SIDEBAR_WIDTH,sidebar_h},0x202131);
    pg_window_text(w,16,TOOLBAR_HEIGHT+14,"CATEGORIES",0x7F849C);
    struct pg_rect r0={8,TOOLBAR_HEIGHT+36,SIDEBAR_WIDTH-16,34};
    struct pg_rect r1={8,TOOLBAR_HEIGHT+74,SIDEBAR_WIDTH-16,34};
    pg_window_rect(w,r0,app->tab==0?0x36384D:0x202131);
    pg_window_rect(w,r1,app->tab==1?0x36384D:0x202131);
    pg_window_text(w,22,TOOLBAR_HEIGHT+48,"Sound",app->tab==0?w->theme.text:0xCDD6F4);
    pg_window_text(w,22,TOOLBAR_HEIGHT+86,"Wi-Fi",app->tab==1?w->theme.text:0xCDD6F4);
    pg_window_text(w,16,TOOLBAR_HEIGHT+130,"INFO",0x7F849C);
    pg_window_text(w,16,TOOLBAR_HEIGHT+150,"PureC OS",w->theme.muted_text);
    if(clicked(w,r0,ev)) app->tab=0;
    if(clicked(w,r1,ev)) app->tab=1;
    if(app->tab==0) audio_page_draw(w,&app->model,ev);
    else wifi_page_draw(w,ev);
    pg_window_rect(w,(struct pg_rect){0,ch-STATUS_HEIGHT,cw,STATUS_HEIGHT},0x292A3D);
    pg_window_text(w,12,ch-19,app->tab==0?"Sound • changes saved instantly":"Wi-Fi • coming soon",0xBAC2DE);
    pg_window_end(w);
}
