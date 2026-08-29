#include "settings/audio_page.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#define SIDEBAR_WIDTH 150
#define TOOLBAR_HEIGHT 46
static void write_u32(char *o,uint32_t v){ char t[12];int p=11;t[p--]='\0';if(v==0)t[p--]='0';else while(v>0&&p>=0){t[p--]='0'+v%10;v/=10;}int i=0;for(int k=p+1;t[k];k++)o[i++]=t[k];o[i]='\0';}
void audio_page_draw(struct pg_window *w, struct settings_model *m, const struct pg_event *ev){
    uint32_t left=SIDEBAR_WIDTH+14; uint32_t top=TOOLBAR_HEIGHT+12;
    uint32_t width=w->client.width-left-14;
    pg_window_rect(w,(struct pg_rect){left,top,width,78},0x2B2D40);
    pg_window_text(w,left+14,top+14,"Volume",w->theme.text);
    char vs[8]; write_u32(vs,(uint32_t)m->volume);
    char vl[16]; pc_copy(vl,vs,sizeof(vl)); uint32_t l=pc_strlen(vl); vl[l++]='%'; if(m->muted){vl[l++]=' ';vl[l++]='(';vl[l++]='m';vl[l++]='u';vl[l++]='t';vl[l++]='e';vl[l++]='d';vl[l++] =')';} vl[l]='\0';
    pg_window_text(w,left+width-90,top+14,vl,m->muted?0xF38BA8:w->theme.accent);
    uint32_t bar_x=left+14; uint32_t bar_y=top+40; uint32_t bar_w=width-28; uint32_t bar_h=8;
    pg_window_rect(w,(struct pg_rect){bar_x,bar_y,bar_w,bar_h},0x181825);
    uint32_t fill=(bar_w*(uint32_t)m->volume)/100; if(fill) pg_window_rect(w,(struct pg_rect){bar_x,bar_y,fill,bar_h},m->muted?0x585B70:0x89B4FA);
    if(pg_button(w,(struct pg_rect){left+14,top+54,44,18},"-",ev)&&ev->type==PG_EVENT_MOUSE_DOWN){ m->volume=m->volume>=5?m->volume-5:0; settings_model_apply(m); settings_model_save(m);}
    if(pg_button(w,(struct pg_rect){left+64,top+54,44,18},"+",ev)&&ev->type==PG_EVENT_MOUSE_DOWN){ m->volume=m->volume+5>100?100:m->volume+5; settings_model_apply(m); settings_model_save(m);}
    if(pg_button(w,(struct pg_rect){left+116,top+54,72,18},m->muted?"Unmute":"Mute",ev)&&ev->type==PG_EVENT_MOUSE_DOWN){ m->muted^=1; settings_model_apply(m); settings_model_save(m);}
    if(pg_button(w,(struct pg_rect){left+194,top+54,64,18},"Test",ev)&&ev->type==PG_EVENT_MOUSE_DOWN) pc_audio_play_test();
    uint32_t top2=top+92;
    pg_window_rect(w,(struct pg_rect){left,top2,width,92},0x2B2D40);
    struct audio_status st={0}; pc_audio_get_status(&st);
    pg_window_text(w,left+14,top2+14,"Output device",w->theme.text);
    char be[20]; if(st.backend==2) pc_copy(be,"HDA",sizeof(be)); else if(st.backend==1) pc_copy(be,"PC speaker",sizeof(be)); else pc_copy(be,"None",sizeof(be));
    pg_window_text(w,left+14,top2+34,be,w->theme.muted_text);
    char sel[20]="Device "; char ns[8]; char cs[8]; write_u32(ns,st.selected_output_device); write_u32(cs,st.output_device_count?st.output_device_count:1);
    uint32_t dl=pc_strlen(sel); for(int i=0;ns[i];i++) sel[dl++]=ns[i]; sel[dl++]='/'; for(int i=0;cs[i];i++) sel[dl++]=cs[i]; sel[dl]='\0';
    pg_window_text(w,left+width-110,top2+34,sel,0xBAC2DE);
    char hc[24]="Codec "; char hcs[8]; write_u32(hcs,st.hda_codec); uint32_t hl=pc_strlen(hc); for(int i=0;hcs[i];i++) hc[hl++]=hcs[i]; hc[hl]='\0';
    if(st.backend==2) pg_window_text(w,left+14,top2+54,hc,w->theme.muted_text); else pg_window_text(w,left+14,top2+54,"No advanced devices",w->theme.muted_text);
    if(pg_button(w,(struct pg_rect){left+14,top2+68,72,18},"Prev",ev)&&ev->type==PG_EVENT_MOUSE_DOWN){ uint32_t c=st.output_device_count?st.output_device_count:1; m->device=(int)((st.selected_output_device+c-1)%c); pc_audio_select_output((uint32_t)m->device); settings_model_save(m); }
    if(pg_button(w,(struct pg_rect){left+92,top2+68,72,18},"Next",ev)&&ev->type==PG_EVENT_MOUSE_DOWN){ uint32_t c=st.output_device_count?st.output_device_count:1; m->device=(int)((st.selected_output_device+1)%c); pc_audio_select_output((uint32_t)m->device); settings_model_save(m); }
}
