#include "desktop_apps.h"
#include "../syscall.h"
#include "../../drivers/gop.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../kernel/syscall.h"
#include "../../kernel/system_info.h"
#include "../../lib/string.h"

#define DATETIME_PATH "/purec/datetime.cfg"
struct datetime { uint16_t year; uint8_t month,day,hour,minute,second; };
static struct datetime value={2026,8,28,12,0,0};
static uint64_t last_uptime_second;
static enum desktop_app active_app;
static bool visible,dragging,editing;
static uint32_t x=180,y=90,w=360,h=270;
static int32_t drag_x,drag_y;
static char input[40]; static uint32_t input_length;
static int64_t calc_left; static char calc_op; static bool calc_have_left;
static const char *message="";

static bool leap(uint16_t year){ return (year%4==0 && year%100!=0)||year%400==0; }
static uint8_t days_in_month(uint16_t year,uint8_t month){
    static const uint8_t days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(month<1||month>12) return 0;
    return month==2 && leap(year) ? 29 : days[month-1];
}
static void tick_second(void){
    if(++value.second<60) return; value.second=0;
    if(++value.minute<60) return; value.minute=0;
    if(++value.hour<24) return; value.hour=0;
    if(++value.day<=days_in_month(value.year,value.month)) return; value.day=1;
    if(++value.month<=12) return; value.month=1; value.year++;
}
static char *append_u(char *out,uint32_t number,uint8_t digits){
    char tmp[12]; uint8_t length=0;
    do{ tmp[length++]=(char)('0'+number%10); number/=10; }while(number);
    while(length<digits) tmp[length++]='0';
    while(length) *out++=tmp[--length]; *out='\0'; return out;
}
static void format_datetime(char out[20]){
    char *p=out; p=append_u(p,value.year,4); *p++='-'; p=append_u(p,value.month,2); *p++='-';
    p=append_u(p,value.day,2); *p++=' '; p=append_u(p,value.hour,2); *p++=':';
    p=append_u(p,value.minute,2); *p++=':'; (void)append_u(p,value.second,2);
}
static bool parse_number(const char *text,uint32_t start,uint32_t count,uint32_t *out){
    uint32_t result=0; for(uint32_t i=0;i<count;i++){ char c=text[start+i]; if(c<'0'||c>'9') return false; result=result*10+(uint32_t)(c-'0'); }
    *out=result; return true;
}
static bool parse_datetime(const char *text,struct datetime *out,bool date_only){
    uint32_t yy,mo,dd,hh=0,mi=0,ss=0;
    if(strlen(text)<10||text[4]!='-'||text[7]!='-'||!parse_number(text,0,4,&yy)||!parse_number(text,5,2,&mo)||!parse_number(text,8,2,&dd)) return false;
    if(!date_only){ if(strlen(text)!=19||text[10]!=' '||text[13]!=':'||text[16]!=':'||!parse_number(text,11,2,&hh)||!parse_number(text,14,2,&mi)||!parse_number(text,17,2,&ss)) return false; }
    if(yy<1980||yy>9999||mo<1||mo>12||dd<1||dd>days_in_month((uint16_t)yy,(uint8_t)mo)||hh>23||mi>59||ss>59) return false;
    *out=(struct datetime){(uint16_t)yy,(uint8_t)mo,(uint8_t)dd,(uint8_t)hh,(uint8_t)mi,(uint8_t)ss}; return true;
}
static void save_datetime(void){
    char text[20]; format_datetime(text);
    (void)userspace_syscall(SYS_DIR_CREATE,(uint64_t)"/purec",0,0);
    int64_t result=userspace_syscall(SYS_FILE_WRITE,(uint64_t)DATETIME_PATH,(uint64_t)text,19);
    message=result>=0 ? "Saved to /purec/datetime.cfg" : "Cannot save date/time";
}
void desktop_apps_save_time(void){
    save_datetime();
}
void desktop_apps_init(void){
    int64_t descriptor=userspace_syscall(SYS_FILE_OPEN,(uint64_t)DATETIME_PATH,0,0);
    if(descriptor>=0){ char text[20]={0}; int64_t count=userspace_syscall(SYS_FILE_READ,(uint64_t)descriptor,(uint64_t)text,19); (void)userspace_syscall(SYS_FILE_CLOSE,(uint64_t)descriptor,0,0); struct datetime loaded; if(count>=10&&parse_datetime(text,&loaded,count<19)) value=loaded; }
    last_uptime_second=system_info_uptime_ms()/1000;
}
static bool inside(int32_t px,int32_t py,uint32_t l,uint32_t t,uint32_t width,uint32_t height){ return px>=(int32_t)l&&py>=(int32_t)t&&px<(int32_t)(l+width)&&py<(int32_t)(t+height); }
static void draw_text(uint32_t ox,uint32_t oy,const char *text,uint32_t color,uint32_t size){ gop_draw_text_sized_at(x+ox,y+oy,text,color,0x1E1E2E,size); }
static uint8_t weekday(uint16_t year,uint8_t month,uint8_t day){
    if(month<3){month+=12;year--;}
    uint16_t k=year%100,j=year/100;
    return (uint8_t)((day+(13*(month+1))/5+k+k/4+j/4+5*j+6)%7);
}
static void draw_calendar_grid(void){
    draw_text(42,108,"Mo Tu We Th Fr Sa Su",0x9399B2,8);
    uint8_t first=weekday(value.year,value.month,1);
    uint8_t monday_first=first==0?6:first-1;
    uint8_t maximum=days_in_month(value.year,value.month),day=1;
    for(uint8_t row=0;row<6&&day<=maximum;row++){
        char line[24];uint8_t position=0;
        for(uint8_t column=0;column<7;column++){
            if((row==0&&column<monday_first)||day>maximum){line[position++]=' ';line[position++]=' ';}
            else {line[position++]=day>=10?(char)('0'+day/10):' ';line[position++]=(char)('0'+day%10);day++;}
            if(column<6)line[position++]=' ';
        }
        line[position]='\0';draw_text(42,124+row*15,line,0xCDD6F4,8);
    }
}
void desktop_apps_draw(void){
    if(!visible) return; mouse_begin_framebuffer_update();
    gop_draw_rect(x+5,y+5,w,h,0x11111B); gop_draw_rect(x,y,w,h,0x45475A); gop_draw_rect(x+1,y+1,w-2,h-2,0x1E1E2E); gop_draw_rect(x+1,y+1,w-2,30,0x89B4FA);
    const char *title=active_app==DESKTOP_APP_CLOCK?"Clock":active_app==DESKTOP_APP_CALCULATOR?"Calculator":"Calendar";
    gop_draw_text_at(x+12,y+10,title,0x1E1E2E,0x89B4FA); gop_draw_rect(x+w-27,y+6,18,18,0xF38BA8); gop_draw_text_at(x+w-23,y+10,"x",0x1E1E2E,0xF38BA8);
    if(active_app==DESKTOP_APP_CLOCK){ char text[20]; format_datetime(text); draw_text(28,70,text,0xCDD6F4,18); draw_text(22,130,"Press E to set YYYY-MM-DD HH:MM:SS",0x9399B2,8); }
    else if(active_app==DESKTOP_APP_CALENDAR){ char text[20]; format_datetime(text); text[10]='\0'; draw_text(92,42,text,0xCDD6F4,14); static const char *months[]={"January","February","March","April","May","June","July","August","September","October","November","December"}; draw_text(120,76,months[value.month-1],0xF9E2AF,12); draw_calendar_grid(); draw_text(22,222,"Press E to set YYYY-MM-DD",0x9399B2,8); }
    else { draw_text(20,52,input[0]?input:"0",0xCDD6F4,16); draw_text(20,105,"Keyboard: 0-9  + - * /  Enter  C",0x9399B2,8); }
    if(editing){ gop_draw_rect(x+15,y+h-66,w-30,28,0x313244); draw_text(22,h-57,input,0xCDD6F4,8); }
    draw_text(18,h-26,message,0xA6E3A1,8); mouse_end_framebuffer_update();
}
void desktop_apps_open(enum desktop_app app,uint32_t sw,uint32_t sh){ active_app=app; visible=true; editing=false; message=""; input[0]='\0'; input_length=0; calc_have_left=false; if(x+w>sw)x=10;if(y+h>sh)y=34; desktop_apps_draw(); }
bool desktop_apps_is_visible(void){return visible;} bool desktop_apps_contains_point(int32_t px,int32_t py){return visible&&inside(px,py,x,y,w,h);}
bool desktop_apps_handle_mouse(int32_t px,int32_t py,uint8_t buttons,bool pressed,bool released,uint32_t sw,uint32_t sh){ if(!visible)return false;if(pressed&&inside(px,py,x+w-27,y+6,18,18)){visible=false;return true;}if(pressed&&inside(px,py,x,y,w,30)){dragging=true;drag_x=px-(int32_t)x;drag_y=py-(int32_t)y;}if(dragging&&(buttons&1)){int32_t nx=px-drag_x,ny=py-drag_y;if(nx<0)nx=0;if(ny<28)ny=28;if(nx>(int32_t)sw-(int32_t)w)nx=(int32_t)sw-(int32_t)w;if(ny>(int32_t)sh-(int32_t)h)ny=(int32_t)sh-(int32_t)h;x=(uint32_t)nx;y=(uint32_t)ny;return true;}if(released&&dragging){dragging=false;return true;}return desktop_apps_contains_point(px,py); }
static int64_t parse_calc(void){ int64_t result=0; for(uint32_t i=0;i<input_length;i++)if(input[i]>='0'&&input[i]<='9')result=result*10+(input[i]-'0');return result; }
bool desktop_apps_handle_key(char key){ if(!visible)return false;
    if(active_app!=DESKTOP_APP_CALCULATOR){ if(!editing){if(key=='e'||key=='E'){editing=true;input_length=0;input[0]='\0';message=active_app==DESKTOP_APP_CLOCK?"Enter full date and time":"Enter date";}else return false;}else if(key==27){editing=false;}else if(key=='\n'||key=='\r'){struct datetime parsed;if(parse_datetime(input,&parsed,active_app==DESKTOP_APP_CALENDAR)){if(active_app==DESKTOP_APP_CALENDAR){parsed.hour=value.hour;parsed.minute=value.minute;parsed.second=value.second;}value=parsed;last_uptime_second=system_info_uptime_ms()/1000;save_datetime();editing=false;}else message="Invalid date/time";}else if((key=='\b'||key==127)&&input_length)input[--input_length]='\0';else if(key>=' '&&key<='~'&&input_length+1<sizeof(input)){input[input_length++]=key;input[input_length]='\0';}}
    else { if(key=='c'||key=='C'){input_length=0;input[0]='\0';calc_have_left=false;message="";}else if(key>='0'&&key<='9'&&input_length+1<sizeof(input)){input[input_length++]=key;input[input_length]='\0';}else if((key=='+'||key=='-'||key=='*'||key=='/')&&input_length){calc_left=parse_calc();calc_op=key;calc_have_left=true;input_length=0;input[0]='\0';}else if((key=='\n'||key=='=')&&calc_have_left&&input_length){int64_t right=parse_calc(),answer=0;bool ok=true;if(calc_op=='+')answer=calc_left+right;else if(calc_op=='-')answer=calc_left-right;else if(calc_op=='*')answer=calc_left*right;else if(right)answer=calc_left/right;else ok=false;input_length=0;char reverse[24];uint32_t n=0;bool neg=answer<0;uint64_t magnitude=neg?(uint64_t)(-(answer+1))+1:(uint64_t)answer;do{reverse[n++]=(char)('0'+magnitude%10);magnitude/=10;}while(magnitude);if(neg)input[input_length++]='-';while(n)input[input_length++]=reverse[--n];input[input_length]='\0';message=ok?"Result":"Division by zero";calc_have_left=false;}}
    desktop_apps_draw();return true;
}
void desktop_apps_update(void){ uint64_t now=system_info_uptime_ms()/1000;while(last_uptime_second<now){tick_second();last_uptime_second++;}static uint64_t drawn;if(visible&&active_app!=DESKTOP_APP_CALCULATOR&&drawn!=now){drawn=now;desktop_apps_draw();} }
