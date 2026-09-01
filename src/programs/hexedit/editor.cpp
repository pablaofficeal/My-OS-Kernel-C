#include "editor.hpp"
#include "window.hpp"
extern "C" {
#include "../../libc/include/purec.h"
#include "../../fs/fs_types.h"
}
#include <stdint.h>

namespace {

constexpr uint32_t BYTES_PER_ROW = 16;
constexpr uint32_t TOOLBAR_H = 28;
constexpr uint32_t HEADER_H  = 16;
constexpr uint32_t STATUS_H  = 26;
constexpr uint32_t ROW_H     = 16;
constexpr uint32_t SIDEBAR_W = 240;
constexpr uint32_t OFFSET_CHARS = 10;

HexWindow win;

// file buffer
uint8_t* buf = nullptr;
uint32_t buf_len = 0;
uint32_t buf_cap = 0;
uint32_t cursor = 0; // byte offset
bool high_nibble = true;
bool hex_mode = true;
bool insert_mode = false;
bool dirty = false;
uint32_t view_offset = 0;

char file_path[128] = "";
char dir_path[128] = "/";

// sidebar
struct DirEntry {
    char name[64];
    bool is_dir;
    uint64_t size;
};
DirEntry entries[64];
int32_t entry_count = 0;
int32_t sel_entry = -1;
uint32_t tree_scroll = 0;
uint32_t tree_visible_rows = 0;

enum PromptMode { PROMPT_NONE, PROMPT_GOTO, PROMPT_SEARCH };
PromptMode prompt = PROMPT_NONE;
char prompt_buf[64] = "";
char status_msg[96] = "";
bool status_err = false;

void set_status(const char* m, bool err){ pc_copy(status_msg, m?m:"", sizeof(status_msg)); status_err=err; }

char hex_digit(uint8_t v){ v&=0xF; return v<10 ? char('0'+v) : char('A'+v-10); }
int hex_val(char c){ if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; if(c>='A'&&c<='F') return c-'A'+10; return -1; }

void u32_hex(char* out, uint32_t v, uint32_t d){
    for(int i=int(d)-1;i>=0;--i){ out[i]=hex_digit(v&0xF); v>>=4; }
    out[d]='\0';
}
void u32_dec(char* out, uint32_t v){
    char rev[11]; uint32_t n=0;
    if(v==0) rev[n++]='0'; else while(v){ rev[n++]=char('0'+v%10); v/=10; }
    uint32_t i=0; while(n) out[i++]=rev[--n]; out[i]='\0';
}
void append_txt(char* dst, const char* src, uint32_t cap){
    uint32_t pos=pc_strlen(dst);
    if(pos>=cap) return;
    pc_copy(dst+pos, src, cap-pos);
}

bool reserve(uint32_t need){
    if(need<=buf_cap) return true;
    uint32_t cap = buf_cap?buf_cap:4096;
    while(cap<need){ if(cap>UINT32_MAX/2){cap=need;break;} cap*=2; }
    void* p = pc_heap_grow(cap-buf_cap);
    if(!p) return false;
    if(buf && p != (void*)(buf+buf_cap)) return false;
    if(!buf) buf=(uint8_t*)p;
    buf_cap=cap;
    return true;
}

uint32_t visible_rows(){
    auto c = win.client();
    if(c.height < TOOLBAR_H+HEADER_H+STATUS_H+ROW_H) return 1;
    return (c.height - TOOLBAR_H - HEADER_H - STATUS_H)/ROW_H;
}
uint32_t total_rows(){ if(buf_len==0) return 1; return (buf_len + BYTES_PER_ROW-1)/BYTES_PER_ROW; }
void clamp_view(){
    uint32_t rows=visible_rows();
    uint32_t t=total_rows();
    uint32_t max_off=0;
    if(t>rows) max_off=(t-rows)*BYTES_PER_ROW;
    if(view_offset>max_off) view_offset=max_off;
    view_offset=(view_offset/BYTES_PER_ROW)*BYTES_PER_ROW;
}
void ensure_visible(){
    uint32_t rows=visible_rows();
    uint32_t cr=cursor/BYTES_PER_ROW;
    uint32_t vr=view_offset/BYTES_PER_ROW;
    if(cr<vr) view_offset=cr*BYTES_PER_ROW;
    else if(cr>=vr+rows) view_offset=(cr-rows+1)*BYTES_PER_ROW;
    clamp_view();
}

// dir helpers
bool is_dir_path(const char* p){
    fs_directory_entry tmp[1];
    int32_t r=pc_directory_list(p, tmp, 1);
    return r>=0;
}
void normalize_dir(char* out, uint32_t cap, const char* src){
    if(!src||!src[0]){ pc_copy(out,"/",cap); return; }
    pc_copy(out,src,cap);
    uint32_t l=pc_strlen(out);
    if(l>1 && out[l-1]=='/') out[l-1]='\0';
    if(out[0]!='/' ){ char tmp[128]; pc_copy(tmp,out,sizeof(tmp)); pc_copy(out,"/",cap); if(tmp[0]){ uint32_t cur=pc_strlen(out); if(cur>1) append_txt(out,"/",cap); append_txt(out,tmp,cap);} }
}
bool join_path(char* out,uint32_t cap,const char* dir,const char* name){
    pc_copy(out,dir,cap);
    uint32_t l=pc_strlen(out);
    if(l+1+pc_strlen(name)+1>cap) return false;
    if(l>1) out[l++]='/';
    else if(out[0]!='/'){ out[0]='/'; out[1]='\0'; l=1; }
    for(uint32_t i=0;name[i];++i) out[l++]=name[i];
    out[l]='\0';
    return true;
}
void parent_dir(char* out,uint32_t cap,const char* dir){
    if(pc_strlen(dir)<=1){ pc_copy(out,"/",cap); return; }
    pc_copy(out,dir,cap);
    uint32_t l=pc_strlen(out);
    while(l>1 && out[l-1]=='/') out[--l]='\0';
    for(int i=int(l)-1;i>0;--i) if(out[i]=='/'){ out[i]='\0'; if(out[0]=='\0') pc_copy(out,"/",cap); return; }
    pc_copy(out,"/",cap);
}

void refresh_dir(){
    entry_count=0; sel_entry=-1;
    fs_directory_entry raw[64];
    int32_t r=pc_directory_list(dir_path, raw, 64);
    if(r<0){ entry_count=0; return; }
    entry_count=r;
    for(int i=0;i<entry_count;++i){
        pc_copy(entries[i].name, raw[i].name, sizeof(entries[i].name));
        entries[i].is_dir = (raw[i].attributes & FS_ATTRIBUTE_DIRECTORY)!=0;
        entries[i].size = raw[i].size;
    }
    // sort: dirs first then name
    for(int i=0;i<entry_count;++i) for(int j=i+1;j<entry_count;++j){
        bool si=entries[i].is_dir, sj=entries[j].is_dir;
        if(si!=sj && sj){ DirEntry t=entries[i]; entries[i]=entries[j]; entries[j]=t; }
        else if(si==sj && pc_strcmp(entries[i].name, entries[j].name)>0){ DirEntry t=entries[i]; entries[i]=entries[j]; entries[j]=t; }
    }
    tree_scroll=0;
}

bool load_file(const char* path){
    // try open
    int32_t d=pc_file_open(path);
    if(d<0){
        // new file: clear buffer
        buf_len=0; cursor=0; view_offset=0; high_nibble=true; dirty=false;
        pc_copy(file_path, path, sizeof(file_path));
        // dir = parent
        char tmp[128]; pc_copy(tmp,path,sizeof(tmp));
        // strip filename
        int l=pc_strlen(tmp); for(int i=l-1;i>=0;--i) if(tmp[i]=='/'){ tmp[i]='\0'; if(tmp[0]=='\0') pc_copy(tmp,"/",sizeof(tmp)); break; }
        normalize_dir(dir_path,sizeof(dir_path),tmp);
        refresh_dir();
        set_status("New file", false);
        return true;
    }
    buf_len=0; cursor=0; view_offset=0; high_nibble=true; dirty=false;
    if(!reserve(4096)) { pc_file_close(d); return false; }
    for(;;){
        uint8_t chunk[512];
        int32_t c=pc_file_read(d,chunk,sizeof(chunk));
        if(c<0){ pc_file_close(d); return false; }
        if(c==0) break;
        if(!reserve(buf_len+uint32_t(c))){ pc_file_close(d); return false; }
        for(int i=0;i<c;++i) buf[buf_len++]=chunk[i];
    }
    pc_file_close(d);
    pc_copy(file_path, path, sizeof(file_path));
    {
        char tmp[128]; pc_copy(tmp,path,sizeof(tmp));
        int l=pc_strlen(tmp); for(int i=l-1;i>=0;--i) if(tmp[i]=='/'){ tmp[i]='\0'; if(tmp[0]=='\0') pc_copy(tmp,"/",sizeof(tmp)); break; }
        normalize_dir(dir_path,sizeof(dir_path),tmp);
        refresh_dir();
        // select entry matching file
        const char* base = path; for(const char* p=path;*p;++p) if(*p=='/') base=p+1;
        for(int i=0;i<entry_count;++i) if(pc_strcmp(entries[i].name, base)==0) sel_entry=i;
    }
    set_status("", false);
    return true;
}

bool save_file(){
    if(!file_path[0]) return false;
    if(pc_file_write(file_path, buf, buf_len)<0) return false;
    dirty=false; return true;
}

bool insert_byte(uint32_t off, uint8_t v){
    if(off>buf_len) off=buf_len;
    if(!reserve(buf_len+1)) return false;
    for(uint32_t i=buf_len;i>off;--i) buf[i]=buf[i-1];
    buf[off]=v; buf_len++;
    return true;
}
void delete_byte(uint32_t off){
    if(off>=buf_len) return;
    for(uint32_t i=off;i+1<buf_len;++i) buf[i]=buf[i+1];
    buf_len--; if(cursor>buf_len) cursor=buf_len;
}

// drawing
void draw_toolbar(){
    auto *w=&win.gui;
    auto c=win.client();
    pg_window_rect(w,{0,0,c.width,TOOLBAR_H},0x252638);
    pg_window_text(w,8,8, file_path[0]?file_path:"(no file)", w->theme.text);
    char info[64]=""; char dec[12]; u32_dec(dec, buf_len); pc_copy(info,dec,sizeof(info)); append_txt(info," bytes",sizeof(info)); if(dirty) append_txt(info," *",sizeof(info));
    pg_window_text(w, c.width - pc_strlen(info)*8 - 8, 8, info, dirty?0xF9E2AF:w->theme.muted_text);
    const char* mode=hex_mode?"HEX":"ASCII"; const char* im=insert_mode?"INS":"OVR"; char ms[20]=""; pc_copy(ms,mode,sizeof(ms)); append_txt(ms,"/",sizeof(ms)); append_txt(ms,im,sizeof(ms));
    pg_window_text(w, c.width/2-20, 8, ms, hex_mode?0x89B4FA:0xA6E3A1);
}
void draw_header(uint32_t hex_x){
    auto *w=&win.gui;
    auto c=win.client();
    uint32_t y=TOOLBAR_H;
    // header spans right pane only
    pg_window_rect(w,{SIDEBAR_W, y, c.width - SIDEBAR_W, HEADER_H},0x1E1E2E);
    pg_window_text(w,SIDEBAR_W+8,y+4,"Offset",0x7F849C);
    for(uint32_t i=0;i<BYTES_PER_ROW;++i){
        char h[3]; h[0]=hex_digit(i>>4); h[1]=hex_digit(i); h[2]='\0';
        uint32_t x= hex_x + i*3*8 + (i>=8?4:0);
        pg_window_text(w,x,y+4,h,0x7F849C);
    }
}
void draw_status(){
    auto *w=&win.gui; auto c=win.client(); uint32_t y=c.height-STATUS_H;
    pg_window_rect(w,{0,y,c.width,STATUS_H},0x292A3D);
    if(prompt!=PROMPT_NONE){
        const char* lab = prompt==PROMPT_GOTO? "Goto (hex): ":"Search (ascii): ";
        pg_window_text(w,8,y+8,lab,0xCDD6F4);
        uint32_t lx=pc_strlen(lab)*8+12;
        pg_window_text(w,lx,y+8,prompt_buf,0xA6E3A1);
        uint32_t cx=lx+pc_strlen(prompt_buf)*8; pg_window_rect(w,{cx,y+6,8,14},0x89B4FA);
        pg_window_text(w,c.width-160,y+8,"Enter OK Esc cancel",0x7F849C);
        return;
    }
    char left[96]=""; 
    if(status_msg[0]){ pc_copy(left,status_msg,sizeof(left)); pg_window_text(w,8,y+8,left,status_err?0xF38BA8:0xF9E2AF); }
    else {
        char off[9]; u32_hex(off,cursor,8); pc_copy(left,off,sizeof(left)); append_txt(left,"  ",sizeof(left)); char dec[12]; u32_dec(dec,cursor); append_txt(left,dec,sizeof(left));
        if(cursor<buf_len){ append_txt(left,"  val 0x",sizeof(left)); char hb[3]; hb[0]=hex_digit(buf[cursor]>>4); hb[1]=hex_digit(buf[cursor]); hb[2]='\0'; append_txt(left,hb,sizeof(left)); char a[8]; a[0]=' ';a[1]='\'';a[2]=(buf[cursor]>=32&&buf[cursor]<=126)? char(buf[cursor]):'.';a[3]='\'';a[4]='\0'; append_txt(left,a,sizeof(left)); }
        pg_window_text(w,8,y+8,left,w->theme.muted_text);
    }
    const char* help="Ctrl+S save Ctrl+X exit Ctrl+G goto Ctrl+F find Tab HEX/ASCII F1 OVR/INS";
    uint32_t hw=pc_strlen(help)*8;
    if(!status_msg[0] && hw+160<c.width) pg_window_text(w,c.width-hw-8,y+8,help,0x6C7086);
}

void draw_sidebar(){
    auto *w=&win.gui; auto c=win.client();
    uint32_t top=TOOLBAR_H;
    uint32_t h=c.height-TOOLBAR_H-STATUS_H;
    pg_window_rect(w,{0,top,SIDEBAR_W,h},0x202131);
    // path
    pg_window_text(w,8,top+6,dir_path,0xCDD6F4);
    pg_window_rect(w,{8,top+20,SIDEBAR_W-16,1},0x313244);
    // up button
    bool up_hover=false; // we don't track hover here, just draw
    pg_window_rect(w,{8,top+24, SIDEBAR_W-16,18},0x313244);
    pg_window_text(w,12,top+28,".. (up)",0x89B4FA);
    uint32_t list_top=top+46;
    uint32_t list_h = h - 46;
    tree_visible_rows = list_h / 18;
    if(tree_visible_rows==0) tree_visible_rows=1;
    if(tree_scroll + tree_visible_rows > uint32_t(entry_count) && entry_count>0){
        tree_scroll = entry_count>int32_t(tree_visible_rows)? entry_count - tree_visible_rows : 0;
    }
    for(uint32_t i=0;i<tree_visible_rows;++i){
        uint32_t idx = tree_scroll + i;
        if(int32_t(idx)>=entry_count) break;
        uint32_t y = list_top + i*18;
        bool sel = int32_t(idx)==sel_entry;
        pg_window_rect(w,{6,y, SIDEBAR_W-12,16}, sel?0x45475A:(i%2?0x252638:0x202131));
        // icon
        if(entries[idx].is_dir){
            pg_window_rect(w,{10,y+3,12,8},0xF9E2AF);
            pg_window_rect(w,{10,y+8,14,6},0xF9E2AF);
        } else {
            pg_window_rect(w,{10,y+2,10,12},0xCDD6F4);
        }
        pg_window_text(w,28,y+4,entries[idx].name, sel?0xCDD6F4:(entries[idx].is_dir?0x89B4FA:w->theme.text));
        if(!entries[idx].is_dir){
            char sz[16]=""; // size
            // compact size
            uint64_t s=entries[idx].size;
            if(s>=1024) { char d[12]; u32_dec(d, uint32_t(s/1024)); pc_copy(sz,d,sizeof(sz)); append_txt(sz,"K",sizeof(sz)); }
            else { char d[12]; u32_dec(d,uint32_t(s)); pc_copy(sz,d,sizeof(sz)); }
            pg_window_text(w,SIDEBAR_W-40,y+4,sz,0x9399B2);
        }
    }
    // scrollbar hint
    if(uint32_t(entry_count)>tree_visible_rows){
        uint32_t bar_h = (tree_visible_rows * list_h)/uint32_t(entry_count);
        if(bar_h<12) bar_h=12;
        uint32_t bar_y = list_top + (tree_scroll * (list_h - bar_h))/ (entry_count - tree_visible_rows);
        pg_window_rect(w,{SIDEBAR_W-4, bar_y, 2, bar_h},0x585B70);
    }
}

void draw_hex(uint32_t hex_x, uint32_t ascii_x){
    auto *w=&win.gui; auto c=win.client();
    uint32_t top=TOOLBAR_H+HEADER_H;
    uint32_t rows=visible_rows();
    uint32_t right_w = c.width - SIDEBAR_W;
    pg_window_rect(w,{SIDEBAR_W,top,right_w, rows*ROW_H},0x1E1E2E);
    for(uint32_t r=0;r<rows;++r){
        uint32_t off=view_offset + r*BYTES_PER_ROW;
        uint32_t y=top + r*ROW_H;
        if(r%2==1) pg_window_rect(w,{SIDEBAR_W,y,right_w,ROW_H},0x202131);
        char offs[9]; u32_hex(offs,off,8);
        bool row_cur = cursor>=off && cursor<off+BYTES_PER_ROW;
        pg_window_text(w,SIDEBAR_W+8,y+4,offs,row_cur?0x89B4FA:0x7F849C);
        for(uint32_t col=0;col<BYTES_PER_ROW;++col){
            uint32_t abs=off+col;
            uint32_t x=hex_x + col*3*8 + (col>=8?4:0);
            if(abs<buf_len){
                char hb[3]; hb[0]=hex_digit(buf[abs]>>4); hb[1]=hex_digit(buf[abs]); hb[2]='\0';
                bool cur=abs==cursor;
                if(cur){
                    uint32_t bg=hex_mode?0x89B4FA:0x45475A;
                    uint32_t fg=hex_mode?0x1E1E2E:0xCDD6F4;
                    pg_window_rect(w,{x,y+2,16,12},bg);
                    pg_window_text(w,x,y+4,hb,fg);
                    if(hex_mode && !high_nibble) pg_window_rect(w,{x+8,y+12,8,2},0xF9E2AF);
                } else pg_window_text(w,x,y+4,hb,w->theme.text);
            } else if(abs==buf_len && cursor==buf_len && col==(buf_len%BYTES_PER_ROW)){
                pg_window_rect(w,{x,y+2,16,12}, hex_mode?0x89B4FA:0x45475A);
                pg_window_text(w,x,y+4,"..",hex_mode?0x1E1E2E:0xCDD6F4);
            }
        }
        pg_window_rect(w,{hex_x+BYTES_PER_ROW*3*8+8, y, 1, ROW_H},0x313244);
        for(uint32_t col=0;col<BYTES_PER_ROW;++col){
            uint32_t abs=off+col;
            uint32_t x=ascii_x + col*8;
            if(abs<buf_len){
                char ch=char(buf[abs]);
                char s[2]; s[0]=(ch>=32&&ch<=126)?ch:'.'; s[1]='\0';
                bool cur=abs==cursor;
                if(cur && !hex_mode){ pg_window_rect(w,{x,y+2,8,12},0xA6E3A1); pg_window_text(w,x,y+4,s,0x1E1E2E); }
                else if(cur && hex_mode){ pg_window_rect(w,{x,y+2,8,12},0x45475A); pg_window_text(w,x,y+4,s,w->theme.text); }
                else pg_window_text(w,x,y+4,s,(buf[abs]>=32&&buf[abs]<=126)?w->theme.text:w->theme.muted_text);
            } else if(abs==buf_len && cursor==buf_len && col==(buf_len%BYTES_PER_ROW)){
                pg_window_rect(w,{x,y+2,8,12}, !hex_mode?0xA6E3A1:0x45475A);
                pg_window_text(w,x,y+4," ",!hex_mode?0x1E1E2E:w->theme.text);
            }
        }
    }
}

bool do_draw(){
    if(win.isMinimized()) return true;
    win.begin();
    if(win.isMinimized()){ win.end(); return true; }
    auto c=win.client();
    pg_window_clear(&win.gui,0x1E1E2E);
    draw_toolbar();
    uint32_t hex_x = SIDEBAR_W + 8 + OFFSET_CHARS*8 + 8;
    uint32_t ascii_x = hex_x + BYTES_PER_ROW*3*8 + 4 + 16;
    draw_header(hex_x);
    draw_sidebar();
    // divider
    pg_window_rect(&win.gui,{SIDEBAR_W,TOOLBAR_H,1,c.height-TOOLBAR_H-STATUS_H},0x313244);
    draw_hex(hex_x, ascii_x);
    draw_status();
    win.end();
    return true;
}

// input helpers
void move_cursor(int32_t d){
    int64_t n=int64_t(cursor)+d;
    if(n<0) n=0;
    if(n>int64_t(buf_len)) n=buf_len;
    cursor=uint32_t(n); high_nibble=true; ensure_visible();
}
void handle_hex(char c){
    int v=hex_val(c); if(v<0) return;
    if(cursor==buf_len){
        if(!insert_byte(cursor,uint8_t(v<<4))){ set_status("No memory",true); return; }
        high_nibble=false; dirty=true; set_status("",false); return;
    }
    if(high_nibble){ buf[cursor]=uint8_t((v<<4)|(buf[cursor]&0x0F)); high_nibble=false; }
    else { buf[cursor]=uint8_t((buf[cursor]&0xF0)|v); cursor++; if(cursor>buf_len) cursor=buf_len; high_nibble=true; }
    dirty=true; ensure_visible(); set_status("",false);
}
void handle_ascii(char c){
    if(c<' '||c>'~') return;
    if(cursor==buf_len){ if(!insert_byte(cursor,uint8_t(c))){set_status("No memory",true);return;} cursor++; dirty=true; ensure_visible(); return; }
    if(insert_mode){ if(!insert_byte(cursor,uint8_t(c))){set_status("No memory",true);return;} cursor++; }
    else { buf[cursor++]=uint8_t(c); if(cursor>buf_len) buf_len=cursor; }
    dirty=true; ensure_visible(); set_status("",false);
}
uint32_t parse_hex(const char* s,bool* ok){
    uint32_t v=0; *ok=false; if(!s||!*s) return 0;
    if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')) s+=2;
    if(!*s) return 0;
    while(*s){ int hv=hex_val(*s); if(hv<0) return 0; if(v>(UINT32_MAX>>4)) return 0; v=(v<<4)|uint32_t(hv); ++s; }
    *ok=true; return v;
}
void do_search(){
    if(!prompt_buf[0]){ set_status("Empty search",true); return; }
    uint32_t plen=pc_strlen(prompt_buf);
    uint32_t start=cursor+1; if(start>buf_len) start=0;
    for(uint32_t i=start; i+plen<=buf_len; ++i){
        bool m=true; for(uint32_t j=0;j<plen;++j) if(buf[i+j]!=uint8_t(prompt_buf[j])){m=false;break;}
        if(m){ cursor=i; high_nibble=true; ensure_visible(); set_status("Found",false); return; }
    }
    for(uint32_t i=0;i+plen<=buf_len && i<start; ++i){
        bool m=true; for(uint32_t j=0;j<plen;++j) if(buf[i+j]!=uint8_t(prompt_buf[j])){m=false;break;}
        if(m){ cursor=i; high_nibble=true; ensure_visible(); set_status("Found (wrapped)",false); return; }
    }
    set_status("Not found",true);
}
void enter_prompt(PromptMode m){ prompt=m; prompt_buf[0]='\0'; set_status("",false); }
void exit_prompt(bool ok){
    if(!ok){ prompt=PROMPT_NONE; set_status("Cancelled",false); return; }
    if(prompt==PROMPT_GOTO){ bool good; uint32_t off=parse_hex(prompt_buf,&good); if(!good){set_status("Invalid hex",true);prompt=PROMPT_NONE;return;} if(off>buf_len) off=buf_len; cursor=off; high_nibble=true; ensure_visible(); set_status("Goto done",false); }
    else if(prompt==PROMPT_SEARCH) do_search();
    prompt=PROMPT_NONE;
}
bool handle_prompt_key(int32_t k){
    if(k==27){ exit_prompt(false); return true; }
    if(k=='\r'||k=='\n'){ exit_prompt(true); return true; }
    uint32_t l=pc_strlen(prompt_buf);
    if(k=='\b'||k==127){ if(l) prompt_buf[l-1]='\0'; return true; }
    if(k>=' '&&k<='~' && l+1<sizeof(prompt_buf)){ prompt_buf[l]=char(k); prompt_buf[l+1]='\0'; return true; }
    return true;
}
bool handle_special(uint8_t k){
    if(prompt!=PROMPT_NONE) return false;
    uint32_t rows=visible_rows();
    switch(k){
        case 4: cursor=(cursor/BYTES_PER_ROW)*BYTES_PER_ROW; high_nibble=true; ensure_visible(); return true;
        case 5: { uint32_t rs=(cursor/BYTES_PER_ROW)*BYTES_PER_ROW; uint32_t re=rs+BYTES_PER_ROW; if(re>buf_len) re=buf_len; cursor=re; if(cursor>rs && cursor==re && re!=buf_len) cursor=re-1; high_nibble=true; ensure_visible(); return true; }
        case 6: move_cursor(-int32_t(rows*BYTES_PER_ROW)); return true;
        case 7: move_cursor(int32_t(rows*BYTES_PER_ROW)); return true;
        case 8: if(hex_mode && !high_nibble) high_nibble=true; else if(cursor>0){cursor--; high_nibble=true;} ensure_visible(); return true;
        case 9: if(hex_mode && high_nibble && cursor<buf_len) high_nibble=false; else if(cursor<buf_len){cursor++; high_nibble=true;} else if(cursor==buf_len && high_nibble) high_nibble=false; ensure_visible(); return true;
        case 10: move_cursor(-int32_t(BYTES_PER_ROW)); return true;
        case 11: move_cursor(int32_t(BYTES_PER_ROW)); return true;
        case 12: if(cursor<buf_len){ delete_byte(cursor); dirty=true; set_status("",false);} return true;
        case 1: insert_mode=!insert_mode; set_status(insert_mode?"Insert":"Overwrite",false); return true;
        default: return false;
    }
}
bool handle_mouse(const pg_event* ev){
    if(!ev) return false;
    if(ev->type!=PG_EVENT_MOUSE_UP || ev->button!=1) return false;
    auto c=win.client();
    int32_t mx=ev->x - int32_t(c.x);
    int32_t my=ev->y - int32_t(c.y);
    // sidebar?
    if(mx>=0 && mx<int32_t(SIDEBAR_W)){
        uint32_t top=TOOLBAR_H;
        if(my>=int32_t(top+24) && my<int32_t(top+42)){
            // up
            char p[128]; parent_dir(p,sizeof(p),dir_path); normalize_dir(dir_path,sizeof(dir_path),p); refresh_dir(); return true;
        }
        uint32_t list_top=top+46;
        if(my>=int32_t(list_top)){
            uint32_t row=(uint32_t(my - int32_t(list_top)))/18;
            uint32_t idx=tree_scroll + row;
            if(int32_t(idx)<entry_count){
                if(entries[idx].is_dir){
                    char np[128];
                    if(join_path(np,sizeof(np),dir_path,entries[idx].name)){ normalize_dir(dir_path,sizeof(dir_path),np); refresh_dir();}
                } else {
                    char fp[128];
                    if(join_path(fp,sizeof(fp),dir_path,entries[idx].name)){
                        // if dirty prompt? just load
                        if(dirty){ set_status("Unsaved changes — Ctrl+S to save",true); }
                        sel_entry=int32_t(idx);
                        (void)load_file(fp);
                    }
                }
                return true;
            }
        }
        return false;
    }
    // wheel? not needed
    // hex pane
    uint32_t top=TOOLBAR_H+HEADER_H;
    if(my<int32_t(top) || my>=int32_t(c.height-STATUS_H)) return false;
    uint32_t row=(uint32_t(my - int32_t(top)))/ROW_H;
    uint32_t off=view_offset + row*BYTES_PER_ROW;
    uint32_t hex_x = SIDEBAR_W + 8 + OFFSET_CHARS*8 + 8;
    uint32_t ascii_x = hex_x + BYTES_PER_ROW*3*8 + 4 + 16;
    if(mx>=int32_t(hex_x) && mx<int32_t(hex_x+BYTES_PER_ROW*3*8+4)){
        int32_t rel=mx-int32_t(hex_x);
        uint32_t col;
        if(rel < int32_t(8*3*8)) col=uint32_t(rel)/(3*8);
        else if(rel < int32_t(8*3*8+4)) return false;
        else col=8+ uint32_t(rel - (8*3*8+4))/(3*8);
        if(col>=BYTES_PER_ROW) return false;
        uint32_t tgt=off+col;
        if(tgt>buf_len) tgt=buf_len;
        cursor=tgt;
        uint32_t cx=hex_x + col*3*8 + (col>=8?4:0);
        int32_t within=mx-int32_t(cx);
        high_nibble = within < 8;
        hex_mode=true; ensure_visible(); return true;
    } else if(mx>=int32_t(ascii_x) && mx<int32_t(ascii_x+BYTES_PER_ROW*8)){
        uint32_t col=uint32_t(mx-int32_t(ascii_x))/8;
        if(col>=BYTES_PER_ROW) return false;
        uint32_t tgt=off+col;
        if(tgt>buf_len) tgt=buf_len;
        cursor=tgt; hex_mode=false; high_nibble=true; ensure_visible(); return true;
    }
    return false;
}
bool handle_wheel(int32_t y){
    // y wheel not available via mouse; use PageUp/Down already. But sidebar scroll with mouse wheel would be nice if we had event; ignore
    return false;
}

} // namespace

int hexedit_run(const char* initial_path){
    // setup paths
    if(initial_path && initial_path[0]){
        if(is_dir_path(initial_path)){
            normalize_dir(dir_path,sizeof(dir_path),initial_path);
            file_path[0]='\0'; buf_len=0; cursor=0; view_offset=0; dirty=false;
            if(!reserve(4096)) return 1;
            refresh_dir();
        } else {
            if(!reserve(4096)) return 1;
            if(!load_file(initial_path)){
                // try as dir parent
                normalize_dir(dir_path,sizeof(dir_path),"/");
                refresh_dir();
                // keep as new file
                pc_copy(file_path,initial_path,sizeof(file_path));
                buf_len=0;
            }
        }
    } else {
        // no arg -> root
        normalize_dir(dir_path,sizeof(dir_path),"/");
        file_path[0]='\0'; buf_len=0;
        if(!reserve(4096)) return 1;
        refresh_dir();
        set_status("Select a file from the left tree",false);
    }

    if(!win.init()) return 1;
    if(!do_draw()) return 1;
    bool exit_armed=false;
    for(;;){
        pg_event ev;
        if(!win.poll(&ev)){ pc_sleep(16); continue; }
        if(ev.type==PG_EVENT_CLOSE){ win.shutdown(); return 0; }
        if(ev.type==PG_EVENT_MOVE || ev.type==PG_EVENT_MINIMIZE || ev.type==PG_EVENT_FOCUS || ev.type==PG_EVENT_REPAINT){ (void)do_draw(); continue; }
        if(ev.type==PG_EVENT_MOUSE_DOWN || ev.type==PG_EVENT_MOUSE_UP || ev.type==PG_EVENT_MOUSE_MOVE){
            if(ev.type==PG_EVENT_MOUSE_UP && handle_mouse(&ev)) (void)do_draw();
            // sidebar scroll with wheel? emulate via special? ignore
            continue;
        }
        if(ev.type==PG_EVENT_SPECIAL_KEY){ if(handle_special(uint8_t(ev.key))){ exit_armed=false; (void)do_draw(); } continue; }
        if(ev.type!=PG_EVENT_KEY || win.isMinimized()) continue;
        char ch=char(ev.key);
        if(prompt!=PROMPT_NONE){ handle_prompt_key(ev.key); (void)do_draw(); continue; }
        if(ch==19){ exit_armed=false; if(save_file()) set_status("Saved",false); else set_status(!file_path[0]?"No file selected":"Save failed",true); (void)do_draw(); continue; }
        if(ch==24){ if(dirty && !exit_armed){ exit_armed=true; set_status("Unsaved Ctrl+X again discards",true); (void)do_draw(); continue; } win.shutdown(); return 0; }
        if(ch==7){ exit_armed=false; enter_prompt(PROMPT_GOTO); (void)do_draw(); continue; }
        if(ch==6){ exit_armed=false; enter_prompt(PROMPT_SEARCH); (void)do_draw(); continue; }
        if(ch=='\t'){ hex_mode=!hex_mode; high_nibble=true; (void)do_draw(); continue; }
        if(ch=='\r') ch='\n';
        if(ch=='\b'||ch==127){ if(cursor){ cursor--; delete_byte(cursor); dirty=true; high_nibble=true; ensure_visible(); set_status("",false); (void)do_draw(); } continue; }
        if(ch=='\n'){ move_cursor(BYTES_PER_ROW - (cursor%BYTES_PER_ROW)); (void)do_draw(); continue; }
        if(file_path[0]==0 && buf_len==0){ set_status("Select or create a file first (click left tree)",true); (void)do_draw(); continue; }
        if(hex_mode){ if(hex_val(ch)>=0){ handle_hex(ch); (void)do_draw(); } }
        else { if(ch>=' '&&ch<='~'){ handle_ascii(ch); (void)do_draw(); } }
    }
}
