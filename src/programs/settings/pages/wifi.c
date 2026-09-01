#include "settings/wifi_page.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include <stdint.h>
#include <stdbool.h>

#define PAGE_LEFT 198
#define PAGE_TOP 80

#define WIFI_INI_PATH "/config/wifi.ini"
#define WIFI_INI_DIR "/config"

static struct {
    bool initialized;
    struct wifi_status_info status;
    struct wifi_network_info networks[WIFI_SCAN_MAX];
    int32_t network_count;
    int selected;
    char password[WIFI_PASSWORD_CAPACITY];
    bool password_focused;
    uint32_t password_cursor;
    char message[96];
    uint64_t message_until;
    uint32_t scroll_offset;
    bool pending_scan;
    uint64_t last_poll_ms;
    uint64_t last_scan_request_ms;
} state;

static uint64_t now_ms(void){

    static uint64_t fake=0;
    fake+=80;
    return fake;
}

static bool is_inside(const struct pg_window *w, struct pg_rect b, const struct pg_event *ev){
    if(!ev || ev->type!=PG_EVENT_MOUSE_UP || ev->button!=1) return false;
    int32_t x = ev->x - (int32_t)w->client.x;
    int32_t y = ev->y - (int32_t)w->client.y;
    return x >= (int32_t)b.x && y >= (int32_t)b.y && x < (int32_t)(b.x + b.width) && y < (int32_t)(b.y + b.height);
}

static char *append_text(char *out, const char *text){
    while(*text) *out++ = *text++;
    *out='\0';
    return out;
}
static char *append_u32(char *out, uint32_t v){
    char rev[12]; uint32_t c=0;
    do{ rev[c++] = (char)('0'+ v%10); v/=10; }while(v && c<sizeof(rev));
    while(c) *out++ = rev[--c];
    *out='\0'; return out;
}

static void append_ip(char *out, uint32_t ip){

    char *p=out;
    p=append_u32(p, (ip>>24)&255); *p++='.'; *p='\0';
    p=append_u32(p, (ip>>16)&255); *p++='.'; *p='\0';
    p=append_u32(p, (ip>>8)&255); *p++='.'; *p='\0';
    p=append_u32(p, ip&255);
}

static void load_saved_config(void){
    int32_t fd = pc_file_open(WIFI_INI_PATH);
    if(fd<0) return;
    char buf[256]={0};
    int32_t n = pc_file_read(fd, buf, sizeof(buf)-1);
    (void)pc_file_close(fd);
    if(n<=0) return;
    buf[n]='\0';
    char ssid[WIFI_SSID_CAPACITY]={0};
    char pass[WIFI_PASSWORD_CAPACITY]={0};
    for(char *line=buf; *line; ){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char save=*end; *end='\0';
        if(line[0]=='#' || !line[0]){}
        else if(pc_strlen(line)>5 && line[0]=='s' && line[1]=='s' && line[2]=='i' && line[3]=='d' && line[4]=='='){

            uint32_t i=0;
            const char *src=line+5;
            while(src[i] && i+1<sizeof(ssid)){ ssid[i]=src[i]; i++; }
            ssid[i]='\0';
        } else if(pc_strlen(line)>9 && line[0]=='p' && line[1]=='a'){
            const char *src=line+9;
            uint32_t i=0;
            while(src[i] && i+1<sizeof(pass)){ pass[i]=src[i]; i++; }
            pass[i]='\0';
        }
        if(!save) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
    if(ssid[0]){

        for(int i=0;i<state.network_count;i++){
            if(pc_strcmp(state.networks[i].ssid, ssid)==0){ state.selected=i; break; }
        }

        pc_copy(state.password, pass, sizeof(state.password));
        state.password_cursor = pc_strlen(state.password);
    }
}

static void save_config(const char *ssid, const char *password){
    char buf[256];
    char *p=buf;
    p=append_text(p, "# WiFi test config - PLAINTEXT! Do not use in production\n");
    p=append_text(p, "ssid=");
    p=append_text(p, ssid ? ssid : "");
    p=append_text(p, "\npassword=");
    p=append_text(p, password ? password : "");
    p=append_text(p, "\n");

    (void)pc_directory_create(WIFI_INI_DIR);
    int32_t r = pc_file_write(WIFI_INI_PATH, buf, (uint32_t)(p-buf));
    if(r>=0){
        pc_copy(state.message, "Saved plaintext to /config/wifi.ini (test mode)", sizeof(state.message));
    } else {
        pc_copy(state.message, "Save failed", sizeof(state.message));
    }
    state.message_until = now_ms() + 3000;
}

static void refresh_status(void){
    if(!pc_wifi_status(&state.status)){
        state.status.has_device=0;
    }
    state.network_count = pc_wifi_list(state.networks, WIFI_SCAN_MAX);
    if(state.network_count<0) state.network_count=0;
    if(state.selected >= state.network_count) state.selected = state.network_count-1;
    if(state.network_count==0) state.selected=-1;
}

static const char* security_name(uint8_t s){
    switch(s){
        case WIFI_SECURITY_OPEN: return "Open";
        case WIFI_SECURITY_WEP: return "WEP";
        case WIFI_SECURITY_WPA2: return "WPA2";
        case WIFI_SECURITY_WPA3: return "WPA3";
        case WIFI_SECURITY_WPA2_WPA3: return "WPA2/WPA3";
        default: return "Unknown";
    }
}
static const char* state_name(uint32_t s){
    switch(s){
        case WIFI_STATE_DISCONNECTED: return "Disconnected";
        case WIFI_STATE_SCANNING: return "Scanning";
        case WIFI_STATE_CONNECTING: return "Connecting";
        case WIFI_STATE_CONNECTED: return "Connected";
        case WIFI_STATE_FAILED: return "Failed";
        default: return "Unknown";
    }
}
static int rssi_bars(int8_t rssi){
    if(rssi >= -50) return 4;
    if(rssi >= -60) return 3;
    if(rssi >= -70) return 2;
    if(rssi >= -80) return 1;
    return 0;
}

void wifi_page_draw(struct pg_window *window,const struct pg_event *event){
    uint32_t width = window->client.width - PAGE_LEFT - 22;
    if(width < 400) width = 400;

    if(!state.initialized){
        state.initialized=true;
        state.selected=-1;
        state.password[0]='\0';
        state.password_focused=false;
        state.scroll_offset=0;
        refresh_status();
        load_saved_config();
        if(state.status.has_device && state.status.scan_count==0){
            (void)pc_wifi_scan();
        }
    }

    uint64_t now = now_ms();
    if(now - state.last_poll_ms > 200){
        state.last_poll_ms = now;
        refresh_status();

    }

    if(event && state.password_focused){
        if(event->type==PG_EVENT_KEY){
            int32_t k = event->key;
            if(k==27){
                state.password_focused=false;
            } else if(k=='\b' || k==127){
                uint32_t len = pc_strlen(state.password);
                if(len){ state.password[len-1]='\0'; }
            } else if(k=='\r' || k=='\n'){

                if(state.selected>=0 && state.selected < state.network_count){
                    const char *ssid = state.networks[state.selected].ssid;
                    int32_t rc = pc_wifi_connect(ssid, state.password);
                    if(rc==0){
                        save_config(ssid, state.password);
                        pc_copy(state.message, "Connecting...", sizeof(state.message));
                    } else {
                        pc_copy(state.message, "Connect failed", sizeof(state.message));
                    }
                    state.message_until = now + 3000;
                }
            } else if(k>=32 && k<=126){
                uint32_t len = pc_strlen(state.password);
                if(len+1 < sizeof(state.password)){
                    state.password[len]=(char)k;
                    state.password[len+1]='\0';
                }
            }
        } else if(event->type==PG_EVENT_SPECIAL_KEY){

            if(event->key==14){
                uint32_t len = pc_strlen(state.password);
                if(len){ state.password[len-1]='\0'; }
            }
        }
    }

    pg_window_text(window, PAGE_LEFT, PAGE_TOP-2, "Network", window->theme.text);

    pg_window_text(window, PAGE_LEFT, PAGE_TOP+10, "Wi-Fi  (Intel AX201)  -  passwords stored in plaintext for bring-up test", window->theme.muted_text);

    struct pg_rect status_rect = {PAGE_LEFT, PAGE_TOP+28, width, 112};
    pg_window_rect(window, status_rect, 0x2B2D40);

    char line[96];
    if(state.status.has_device){
        char macbuf[32];
        char *p=macbuf;
        for(int i=0;i<6;i++){
            const char *hex="0123456789ABCDEF";
            if(i) *p++=':';
            *p++=hex[(state.status.mac[i]>>4)&0xF];
            *p++=hex[state.status.mac[i]&0xF];
        }
        *p='\0';

        pc_copy(line, state.status.interface_name, sizeof(line));
        append_text(line+pc_strlen(line), "  ");
        append_text(line+pc_strlen(line), macbuf);

        uint32_t state_col = window->theme.accent;
        if(state.status.state==WIFI_STATE_CONNECTED) state_col=0xA6E3A1;
        else if(state.status.state==WIFI_STATE_FAILED) state_col=window->theme.danger;
        else if(state.status.state==WIFI_STATE_SCANNING || state.status.state==WIFI_STATE_CONNECTING) state_col=0xF9E2AF;
        pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+44, line, window->theme.text);

        char state_line[48];
        pc_copy(state_line, state_name(state.status.state), sizeof(state_line));
        if(state.status.connected){
            append_text(state_line+pc_strlen(state_line), "  ");
            append_text(state_line+pc_strlen(state_line), state.status.ssid);

            char rssibuf[16];
            char *q=rssibuf; *q++=' '; *q++='('; q=append_u32(q, (uint32_t)(state.status.rssi <0 ? -state.status.rssi: state.status.rssi)); *q++='d'; *q++='B'; *q++='m'; *q++=')'; *q='\0';
            append_text(state_line+pc_strlen(state_line), rssibuf);
        }
        pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+62, state_line, state_col);

        if(state.status.connected && state.status.ip_address){
            char ipbuf[32]="IP ";
            append_ip(ipbuf+3, state.status.ip_address);
            pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+80, ipbuf, window->theme.muted_text);
            pg_window_text(window, PAGE_LEFT+14+120, PAGE_TOP+80, "via DHCP (real stack)", window->theme.muted_text);
        } else if(state.status.connected && !state.status.ip_address){
            pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+80, "Associated - waiting for DHCP (real stack)", 0xF9E2AF);
        } else if(state.status.state==WIFI_STATE_SCANNING){

            const char *dots = (now/400)%4==0 ? "Scanning   " : (now/400)%4==1 ? "Scanning.  " : (now/400)%4==2 ? "Scanning.. " : "Scanning...";
            pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+80, dots, 0xF9E2AF);
            pg_window_text(window, PAGE_LEFT+14+110, PAGE_TOP+80, "Continuous background scan every 5s", window->theme.muted_text);
        } else if(state.status.state==WIFI_STATE_CONNECTING){
            char conn[64]="Connecting to ";
            pc_copy(conn+14, state.status.ssid, sizeof(conn)-14);
            pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+80, conn, 0xF9E2AF);
        } else if(state.status.state==WIFI_STATE_FAILED){
            pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+80, "Auth failed - check password (plaintext test)", window->theme.danger);
        } else {
            pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+80, "Not connected - select network below", window->theme.muted_text);
        }

        struct pg_rect scan_btn = {PAGE_LEFT + width - 132, PAGE_TOP+42, 116, 24};
        bool do_scan = pg_button(window, scan_btn, state.status.state==WIFI_STATE_SCANNING ? "Scanning..." : "Scan now", event);
        if(do_scan){
            if(pc_wifi_scan()==0){
                pc_copy(state.message, "Scan triggered", sizeof(state.message));
                state.message_until = now+1500;
            } else {
                pc_copy(state.message, "Scan busy", sizeof(state.message));
                state.message_until = now+1500;
            }
        }

        char scancnt[32];
        char *pp=scancnt; append_text(pp, "Found "); pp=scancnt+pc_strlen(scancnt); pp=append_u32(pp, state.status.scan_count); append_text(pp, " networks");
        pg_window_text(window, PAGE_LEFT+width-132, PAGE_TOP+72, scancnt, window->theme.muted_text);
        if(!state.status.has_device){
            pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+100, "No Wi-Fi device", window->theme.danger);
        }
    } else {
        pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+62, "No Wi-Fi adapter detected (wlan0 missing)", window->theme.danger);
        pg_window_text(window, PAGE_LEFT+14, PAGE_TOP+82, "Real hardware required - e1000 provides wired net in QEMU", window->theme.muted_text);
    }

    uint32_t list_top = PAGE_TOP + 148;
    uint32_t list_height = 208;
    struct pg_rect list_rect = {PAGE_LEFT, list_top, width, list_height};
    pg_window_rect(window, list_rect, 0x2B2D40);
    pg_window_text(window, PAGE_LEFT+14, list_top+10, "Available networks (continuous scan)", window->theme.text);

    pg_window_text(window, PAGE_LEFT+14, list_top+28, "SSID", window->theme.muted_text);
    pg_window_text(window, PAGE_LEFT+ width-150, list_top+28, "Signal  Sec  Ch", window->theme.muted_text);

    uint32_t visible = 5;
    uint32_t row_h = 32;
    uint32_t start_y = list_top+44;
    if(state.network_count==0){
        pg_window_text(window, PAGE_LEFT+14, start_y+12, state.status.state==WIFI_STATE_SCANNING ? "Scanning for networks..." : "No networks - press Scan now", window->theme.muted_text);
    } else {

        if(state.selected>=0){
            if((uint32_t)state.selected < state.scroll_offset) state.scroll_offset = (uint32_t)state.selected;
            if((uint32_t)state.selected >= state.scroll_offset+visible) state.scroll_offset = (uint32_t)state.selected - visible +1;
        }
        for(uint32_t i=0;i<visible;i++){
            uint32_t idx = state.scroll_offset + i;
            if(idx >= (uint32_t)state.network_count) break;
            struct wifi_network_info *net = &state.networks[idx];
            struct pg_rect row = {PAGE_LEFT+8, start_y + i*row_h, width-16, row_h-4};
            bool is_sel = (int)idx == state.selected;
            uint32_t bg = is_sel ? 0x45475A : 0x313244;
            pg_window_rect(window, row, bg);

            pg_window_text(window, row.x+10, row.y+10, net->ssid, is_sel ? window->theme.text : 0xCDD6F4);

            if(state.status.connected && pc_strcmp(state.status.ssid, net->ssid)==0){
                pg_window_text(window, row.x+10+ pc_strlen(net->ssid)*8 +8, row.y+10, "(connected)", 0xA6E3A1);
            }

            int bars = rssi_bars(net->rssi);
            char barstr[8];
            for(int b=0;b<4;b++) barstr[b] = b < bars ? '|' : '.';
            barstr[4]='\0';
            char rssibuf[20];
            char *q=rssibuf;
            for(int b=0;b<4;b++) *q++ = barstr[b];
            *q++=' '; *q++=' ';

            if(net->rssi <0){ *q++='-'; q=append_u32(q, (uint32_t)(-net->rssi)); } else q=append_u32(q, (uint32_t)net->rssi);
            append_text(q, "dBm");
            pg_window_text(window, PAGE_LEFT+ width-150, row.y+10, rssibuf, is_sel ? window->theme.text : window->theme.muted_text);

            char secch[32];
            char *s=secch;
            const char *sn = security_name(net->security);
            pc_copy(s, sn, sizeof(secch));
            s+=pc_strlen(s); *s++=' '; *s++=' ';

            append_text(s, "ch"); s+=2; s=append_u32(s, net->channel);
            pg_window_text(window, PAGE_LEFT+ width-78, row.y+10, secch, window->theme.muted_text);

            if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                int32_t x = event->x - (int32_t)window->client.x;
                int32_t y = event->y - (int32_t)window->client.y;
                if(x >= (int32_t)row.x && x < (int32_t)(row.x+row.width) && y >= (int32_t)row.y && y < (int32_t)(row.y+row.height)){
                    state.selected = (int)idx;

                    if(net->security==WIFI_SECURITY_OPEN){
                        state.password[0]='\0';
                    }

                    if(net->security!=WIFI_SECURITY_OPEN){
                        state.password_focused = true;
                    }
                }
            }
        }

        if(state.scroll_offset>0){
            pg_window_text(window, PAGE_LEFT+width-20, list_top+10, "^", window->theme.accent);
            struct pg_rect up = {PAGE_LEFT+width-24, list_top+6, 16, 16};
            if(is_inside(window, up, event) && state.scroll_offset>0) state.scroll_offset--;
        }
        if(state.scroll_offset+visible < (uint32_t)state.network_count){
            pg_window_text(window, PAGE_LEFT+width-20, list_top+list_height-18, "v", window->theme.accent);
            struct pg_rect down = {PAGE_LEFT+width-24, list_top+list_height-20, 16, 16};
            if(is_inside(window, down, event)) state.scroll_offset++;
        }
    }

    uint32_t conn_top = list_top + list_height + 12;
    uint32_t conn_h = 150;
    struct pg_rect conn_rect = {PAGE_LEFT, conn_top, width, conn_h};
    pg_window_rect(window, conn_rect, 0x2B2D40);
    pg_window_text(window, PAGE_LEFT+14, conn_top+12, "Connection", window->theme.text);

    if(state.selected>=0 && state.selected < state.network_count){
        struct wifi_network_info *sel = &state.networks[state.selected];
        char sel_line[64];
        pc_copy(sel_line, "Selected: ", sizeof(sel_line));
        append_text(sel_line+pc_strlen(sel_line), sel->ssid);
        pg_window_text(window, PAGE_LEFT+14, conn_top+34, sel_line, window->theme.accent);
        char info[64];
        pc_copy(info, security_name(sel->security), sizeof(info));
        append_text(info+pc_strlen(info), "  ");
        char chbuf[16]="ch"; char *c=chbuf+2; c=append_u32(c, sel->channel); *c='\0';
        append_text(info+pc_strlen(info), chbuf);
        append_text(info+pc_strlen(info), "  BSSID ");
        for(int i=0;i<6;i++){
            const char *hex="0123456789ABCDEF";
            char b[3]; b[0]=hex[(sel->bssid[i]>>4)&0xF]; b[1]=hex[sel->bssid[i]&0xF]; b[2]='\0';
            append_text(info+pc_strlen(info), b);
            if(i!=5) append_text(info+pc_strlen(info), ":");
        }
        pg_window_text(window, PAGE_LEFT+14, conn_top+50, info, window->theme.muted_text);

        if(sel->security != WIFI_SECURITY_OPEN){
            pg_window_text(window, PAGE_LEFT+14, conn_top+72, "Password (plaintext test):", window->theme.muted_text);
            struct pg_rect pass_rect = {PAGE_LEFT+14, conn_top+88, width-28-110, 24};
            uint32_t pass_bg = state.password_focused ? 0x45475A : 0x181825;
            pg_window_rect(window, pass_rect, pass_bg);

            if(state.password_focused){

            }

            char disp[64];
            pc_copy(disp, state.password, sizeof(disp));
            pg_window_text(window, pass_rect.x+6, pass_rect.y+8, disp, window->theme.text);

            if(state.password_focused && (now/400)%2==0){
                uint32_t cursor_x = pass_rect.x+6 + pc_strlen(disp)*8;
                if(cursor_x +2 < pass_rect.x+pass_rect.width-4)
                    pg_window_rect(window, (struct pg_rect){cursor_x, pass_rect.y+4, 2, 16}, window->theme.text);
            }

            if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                int32_t x = event->x - (int32_t)window->client.x;
                int32_t y = event->y - (int32_t)window->client.y;
                bool inside = x >= (int32_t)pass_rect.x && x < (int32_t)(pass_rect.x+pass_rect.width) && y >= (int32_t)pass_rect.y && y < (int32_t)(pass_rect.y+pass_rect.height);
                state.password_focused = inside;
            }

            struct pg_rect conn_btn = {pass_rect.x+pass_rect.width+8, pass_rect.y, 100, 24};
            bool do_connect = pg_button(window, conn_btn, state.status.state==WIFI_STATE_CONNECTING ? "Connecting" : "Connect", event);
            if(do_connect){
                if(state.status.state==WIFI_STATE_CONNECTING){

                } else {

                    int32_t rc = pc_wifi_connect(sel->ssid, state.password);
                    if(rc==0){
                        save_config(sel->ssid, state.password);
                        pc_copy(state.message, "Connecting...", sizeof(state.message));
                    } else {
                        pc_copy(state.message, "Connect failed - check input", sizeof(state.message));
                    }
                    state.message_until = now + 3000;
                }
            }
        } else {
            pg_window_text(window, PAGE_LEFT+14, conn_top+72, "Open network - no password required", 0xA6E3A1);
            struct pg_rect conn_btn = {PAGE_LEFT+14, conn_top+88, 100, 24};
            bool do_connect = pg_button(window, conn_btn, state.status.state==WIFI_STATE_CONNECTING ? "Connecting" : "Connect", event);
            if(do_connect){
                int32_t rc = pc_wifi_connect(sel->ssid, "");
                if(rc==0){
                    save_config(sel->ssid, "");
                    pc_copy(state.message, "Connecting to open...", sizeof(state.message));
                } else {
                    pc_copy(state.message, "Connect failed", sizeof(state.message));
                }
                state.message_until = now+3000;
            }
        }

        if(state.status.connected){
            struct pg_rect disc_btn = {PAGE_LEFT+ width-110, conn_top+88, 96, 24};
            bool do_disc = pg_button(window, disc_btn, "Disconnect", event);
            if(do_disc){
                (void)pc_wifi_disconnect();
                pc_copy(state.message, "Disconnected", sizeof(state.message));
                state.message_until = now+2000;
            }
        }
    } else {
        pg_window_text(window, PAGE_LEFT+14, conn_top+34, "Select a network from the list above", window->theme.muted_text);
        pg_window_text(window, PAGE_LEFT+14, conn_top+52, "Continuous scan updates every 5 seconds automatically", window->theme.muted_text);
        pg_window_text(window, PAGE_LEFT+14, conn_top+72, "Real WPA2/WPA3 via existing net stack - DHCP after assoc", window->theme.muted_text);
        pg_window_text(window, PAGE_LEFT+14, conn_top+88, "Check serial log for AX201 PCI+MMIO bring-up status", window->theme.muted_text);
    }

    if(state.message[0] && now < state.message_until){
        pg_window_text(window, PAGE_LEFT+14, conn_top+118, state.message, 0xF9E2AF);
    } else {
        pg_window_text(window, PAGE_LEFT+14, conn_top+118, "Plaintext: /config/wifi.ini - for bring-up test only, do not use in production", window->theme.muted_text);
    }

}
