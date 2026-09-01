#include "editor.h"
#include "window.h"
#include "../../libc/include/purec.h"

#define HEXEDIT_INITIAL_CAPACITY 4096
#define HEXEDIT_READ_CHUNK 512
#define HEXEDIT_BYTES_PER_ROW 16
#define HEXEDIT_TOOLBAR_H 28
#define HEXEDIT_HEADER_H 16
#define HEXEDIT_STATUS_H 26
#define HEXEDIT_ROW_H 16
#define HEXEDIT_OFFSET_CHARS 10

static uint8_t *edit_buffer;
static uint32_t edit_length;
static uint32_t edit_capacity;
static uint32_t edit_cursor; // byte offset 0..length
static bool edit_high_nibble; // true = next hex digit = high nibble
static bool edit_hex_mode; // true = hex pane active, false = ascii pane
static bool edit_insert_mode;
static bool edit_dirty;
static uint32_t edit_view_offset; // file offset of first visible row (aligned)
static const char *edit_path;

static struct hexedit_window edit_window;

// input modes for prompts
enum prompt_mode { PROMPT_NONE, PROMPT_GOTO, PROMPT_SEARCH };
static enum prompt_mode prompt_mode = PROMPT_NONE;
static char prompt_buffer[64];
static char status_msg[96];
static bool status_error;

static void set_status(const char *msg, bool is_error){
    pc_copy(status_msg, msg ? msg : "", sizeof(status_msg));
    status_error = is_error;
}

static bool reserve_buffer(uint32_t required){
    if(required <= edit_capacity) return true;
    uint32_t capacity = edit_capacity ? edit_capacity : HEXEDIT_INITIAL_CAPACITY;
    while(capacity < required){
        if(capacity > UINT32_MAX/2U){ capacity = required; break; }
        capacity *= 2U;
    }
    void *alloc = pc_heap_grow(capacity - edit_capacity);
    if(!alloc) return false;
    if(edit_buffer && alloc != (void*)(edit_buffer + edit_capacity)) return false;
    if(!edit_buffer) edit_buffer = (uint8_t*)alloc;
    edit_capacity = capacity;
    return true;
}

static uint32_t visible_rows(void){
    struct pg_rect client = hexedit_window_client(&edit_window);
    if(client.height < HEXEDIT_TOOLBAR_H + HEXEDIT_HEADER_H + HEXEDIT_STATUS_H + HEXEDIT_ROW_H) return 1;
    uint32_t h = client.height - HEXEDIT_TOOLBAR_H - HEXEDIT_HEADER_H - HEXEDIT_STATUS_H;
    return h / HEXEDIT_ROW_H;
}

static uint32_t total_rows(void){
    if(edit_length==0) return 1;
    return (edit_length + HEXEDIT_BYTES_PER_ROW -1)/HEXEDIT_BYTES_PER_ROW;
}

static void clamp_view(void){
    uint32_t rows = visible_rows();
    uint32_t max_offset = 0;
    uint32_t trows = total_rows();
    if(trows > rows) max_offset = (trows - rows) * HEXEDIT_BYTES_PER_ROW;
    if(edit_view_offset > max_offset) edit_view_offset = max_offset;
    edit_view_offset = (edit_view_offset / HEXEDIT_BYTES_PER_ROW) * HEXEDIT_BYTES_PER_ROW;
}

static void ensure_cursor_visible(void){
    uint32_t rows = visible_rows();
    if(rows==0) return;
    uint32_t cursor_row = edit_cursor / HEXEDIT_BYTES_PER_ROW;
    uint32_t view_row = edit_view_offset / HEXEDIT_BYTES_PER_ROW;
    if(cursor_row < view_row){
        edit_view_offset = cursor_row * HEXEDIT_BYTES_PER_ROW;
    } else if(cursor_row >= view_row + rows){
        edit_view_offset = (cursor_row - rows + 1) * HEXEDIT_BYTES_PER_ROW;
    }
    clamp_view();
}

static char hex_digit(uint8_t v){ v &= 0xF; return v<10 ? (char)('0'+v) : (char)('A'+v-10); }
static int hex_value(char c){
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}

static void append_u32_hex(char *out, uint32_t v, uint32_t digits){
    for(int i=(int)digits-1;i>=0;i--){
        out[i]=hex_digit(v & 0xF);
        v >>= 4;
    }
    out[digits]='\0';
}
static void append_u32_dec(char *out, uint32_t v){
    char rev[11]; uint32_t n=0;
    if(v==0) rev[n++]='0'; else while(v){ rev[n++]=(char)('0'+v%10); v/=10; }
    uint32_t i=0; while(n) out[i++]=rev[--n]; out[i]='\0';
}
static void append_text(char *dst, const char *src, uint32_t cap){
    uint32_t pos=pc_strlen(dst);
    if(pos>=cap) return;
    pc_copy(dst+pos, src, cap-pos);
}

// ---- file IO ----
static int load_file(void){
    int32_t d = pc_file_open(edit_path);
    if(d<0) return 0; // new file
    for(;;){
        uint8_t chunk[HEXEDIT_READ_CHUNK];
        int32_t cnt = pc_file_read(d, chunk, sizeof(chunk));
        if(cnt<0){ (void)pc_file_close(d); return -1; }
        if(cnt==0) break;
        if((uint32_t)cnt > UINT32_MAX - edit_length || !reserve_buffer(edit_length + (uint32_t)cnt)){
            (void)pc_file_close(d); return -2;
        }
        for(int32_t i=0;i<cnt;i++) edit_buffer[edit_length++]=chunk[i];
    }
    (void)pc_file_close(d);
    return 1;
}
static bool save_file(void){
    if(pc_file_write(edit_path, edit_buffer, edit_length)<0) return false;
    edit_dirty=false;
    return true;
}

// ---- buffer editing ----
static bool insert_byte(uint32_t offset, uint8_t value){
    if(offset > edit_length) offset = edit_length;
    if(!reserve_buffer(edit_length+1)) return false;
    for(uint32_t i=edit_length;i>offset;i--) edit_buffer[i]=edit_buffer[i-1];
    edit_buffer[offset]=value;
    edit_length++;
    return true;
}
static void delete_byte(uint32_t offset){
    if(offset >= edit_length) return;
    for(uint32_t i=offset;i+1<edit_length;i++) edit_buffer[i]=edit_buffer[i+1];
    edit_length--;
    if(edit_cursor > edit_length) edit_cursor = edit_length;
}

// ---- rendering helpers ----
static void draw_toolbar(void){
    struct pg_window *w = &edit_window.gui;
    struct pg_rect client = hexedit_window_client(&edit_window);
    pg_window_rect(w, (struct pg_rect){0,0,client.width,HEXEDIT_TOOLBAR_H}, 0x252638);
    char title[128]={0};
    pc_copy(title, edit_path ? edit_path : "(no file)", sizeof(title));
    pg_window_text(w, 8, 8, title, w->theme.text);
    char info[64]={0};
    char dec[12];
    append_u32_dec(dec, edit_length);
    pc_copy(info, dec, sizeof(info));
    append_text(info, " bytes", sizeof(info));
    if(edit_dirty) append_text(info, " *", sizeof(info));
    pg_window_text(w, client.width - pc_strlen(info)*8 - 8, 8, info, edit_dirty?0xF9E2AF:w->theme.muted_text);
    // mode indicator
    const char *mode = edit_hex_mode ? "HEX" : "ASCII";
    const char *ins = edit_insert_mode ? "INS" : "OVR";
    char modestr[20]={0};
    pc_copy(modestr, mode, sizeof(modestr));
    append_text(modestr, "/", sizeof(modestr));
    append_text(modestr, ins, sizeof(modestr));
    pg_window_text(w, client.width/2 - 20, 8, modestr, edit_hex_mode?0x89B4FA:0xA6E3A1);
}

static void draw_header(void){
    struct pg_window *w = &edit_window.gui;
    struct pg_rect client = hexedit_window_client(&edit_window);
    uint32_t y = HEXEDIT_TOOLBAR_H;
    pg_window_rect(w, (struct pg_rect){0,y,client.width,HEXEDIT_HEADER_H}, 0x1E1E2E);
    pg_window_text(w, 8, y+4, "Offset", 0x7F849C);
    uint32_t hex_x = 8 + HEXEDIT_OFFSET_CHARS*8 + 8;
    for(uint32_t i=0;i<HEXEDIT_BYTES_PER_ROW;i++){
        char h[3]; h[0]=hex_digit(i>>4); h[1]=hex_digit(i); h[2]='\0';
        uint32_t x = hex_x + i*3*8 + (i>=8 ? 4 : 0);
        pg_window_text(w, x, y+4, h, 0x7F849C);
    }
    uint32_t ascii_x = hex_x + HEXEDIT_BYTES_PER_ROW*3*8 + 4 + 16;
    pg_window_text(w, ascii_x, y+4, "ASCII", 0x7F849C);
}

static void draw_status(void){
    struct pg_window *w = &edit_window.gui;
    struct pg_rect client = hexedit_window_client(&edit_window);
    uint32_t y = client.height - HEXEDIT_STATUS_H;
    pg_window_rect(w, (struct pg_rect){0,y,client.width,HEXEDIT_STATUS_H}, 0x292A3D);
    if(prompt_mode != PROMPT_NONE){
        const char *label = prompt_mode==PROMPT_GOTO ? "Goto (hex): " : "Search (ascii): ";
        pg_window_text(w, 8, y+8, label, 0xCDD6F4);
        uint32_t lx = pc_strlen(label)*8 + 12;
        pg_window_text(w, lx, y+8, prompt_buffer, 0xA6E3A1);
        // cursor
        uint32_t cx = lx + pc_strlen(prompt_buffer)*8;
        pg_window_rect(w, (struct pg_rect){cx, y+6, 8, 14}, 0x89B4FA);
        // help right
        pg_window_text(w, client.width- 160, y+8, "Enter OK  Esc cancel", 0x7F849C);
        return;
    }
    char left[96]={0};
    if(status_msg[0]){
        pc_copy(left, status_msg, sizeof(left));
        pg_window_text(w, 8, y+8, left, status_error?0xF38BA8:0xF9E2AF);
    } else {
        char off[12]; append_u32_hex(off, edit_cursor, 8);
        pc_copy(left, off, sizeof(left));
        append_text(left, "  ", sizeof(left));
        char dec[12]; append_u32_dec(dec, edit_cursor);
        append_text(left, dec, sizeof(left));
        if(edit_cursor < edit_length){
            append_text(left, "  val 0x", sizeof(left));
            char hb[3]; hb[0]=hex_digit(edit_buffer[edit_cursor]>>4); hb[1]=hex_digit(edit_buffer[edit_cursor]); hb[2]='\0';
            append_text(left, hb, sizeof(left));
            char ascii[8]; ascii[0]=' '; ascii[1]='\''; ascii[2]= (edit_buffer[edit_cursor]>=32 && edit_buffer[edit_cursor]<=126) ? (char)edit_buffer[edit_cursor] : '.'; ascii[3]='\'';
            ascii[4]='\0';
            append_text(left, ascii, sizeof(left));
        }
        pg_window_text(w, 8, y+8, left, w->theme.muted_text);
    }
    const char *help = "Ctrl+S save  Ctrl+X exit  Ctrl+G goto  Ctrl+F find  Tab switch  F1 ins";
    // right-aligned help if no status, else show help dim
    if(!status_msg[0]){
        uint32_t hw = pc_strlen(help)*8;
        if(hw + 160 < client.width) pg_window_text(w, client.width - hw -8, y+8, help, 0x6C7086);
    } else {
        // show short help after status
        uint32_t sx = 8 + pc_strlen(left)*8 +16;
        if(sx+200 < client.width) pg_window_text(w, sx, y+8, "Ctrl+S save | Tab hex/ascii", 0x6C7086);
    }
}

static void draw_hex_rows(void){
    struct pg_window *w = &edit_window.gui;
    struct pg_rect client = hexedit_window_client(&edit_window);
    uint32_t top = HEXEDIT_TOOLBAR_H + HEXEDIT_HEADER_H;
    uint32_t hex_x = 8 + HEXEDIT_OFFSET_CHARS*8 + 8;
    uint32_t ascii_x = hex_x + HEXEDIT_BYTES_PER_ROW*3*8 + 4 + 16;
    uint32_t rows = visible_rows();
    // background for hex area
    pg_window_rect(w, (struct pg_rect){0,top,client.width, rows*HEXEDIT_ROW_H}, 0x1E1E2E);
    for(uint32_t r=0;r<rows;r++){
        uint32_t offset = edit_view_offset + r*HEXEDIT_BYTES_PER_ROW;
        uint32_t y = top + r*HEXEDIT_ROW_H;
        if(r%2==1){
            pg_window_rect(w, (struct pg_rect){0,y,client.width,HEXEDIT_ROW_H}, 0x202131);
        }
        if(offset >= edit_length && r!=0 && offset!=0) {
            // if beyond file and not first row, still show offset but dim
        }
        if(offset > 0xFFFFFFF0) break; // prevent overflow

        // offset
        char off[9]; append_u32_hex(off, offset, 8);
        bool row_contains_cursor = edit_cursor >= offset && edit_cursor < offset+HEXEDIT_BYTES_PER_ROW;
        uint32_t off_color = row_contains_cursor ? 0x89B4FA : 0x7F849C;
        pg_window_text(w, 8, y+4, off, off_color);

        // hex bytes
        for(uint32_t c=0;c<HEXEDIT_BYTES_PER_ROW;c++){
            uint32_t abs = offset + c;
            uint32_t x = hex_x + c*3*8 + (c>=8 ? 4 : 0);
            if(abs < edit_length){
                char hb[3]; hb[0]=hex_digit(edit_buffer[abs]>>4); hb[1]=hex_digit(edit_buffer[abs]); hb[2]='\0';
                bool is_cursor = abs == edit_cursor;
                if(is_cursor){
                    uint32_t bg = edit_hex_mode ? 0x89B4FA : 0x45475A;
                    uint32_t fg = edit_hex_mode ? 0x1E1E2E : 0xCDD6F4;
                    // highlight nibble?
                    if(edit_hex_mode && !edit_high_nibble){
                        // highlight low nibble differently: draw full then overlay low?
                        pg_window_rect(w, (struct pg_rect){x, y+2, 16, 12}, bg);
                        pg_window_text(w, x, y+4, hb, fg);
                        // underline low nibble
                        pg_window_rect(w, (struct pg_rect){x+8, y+12, 8, 2}, 0xF9E2AF);
                    } else {
                        pg_window_rect(w, (struct pg_rect){x, y+2, 16, 12}, bg);
                        pg_window_text(w, x, y+4, hb, fg);
                    }
                } else {
                    pg_window_text(w, x, y+4, hb, w->theme.text);
                }
            } else if(abs == edit_length && edit_cursor==edit_length && c==0){
                // cursor at EOF after last byte: show blinking placeholder
                // actually show underscore at next position
                uint32_t x0 = hex_x + (edit_length % HEXEDIT_BYTES_PER_ROW)*3*8 + ( (edit_length % HEXEDIT_BYTES_PER_ROW)>=8 ?4:0);
                // we already handling via is_cursor? This row's first col after length handled below
                // handled specially: draw cursor box empty
                if(c== (edit_length%HEXEDIT_BYTES_PER_ROW)){
                    bool is_cursor = true;
                    if(is_cursor){
                        pg_window_rect(w, (struct pg_rect){x, y+2, 16, 12}, edit_hex_mode?0x89B4FA:0x45475A);
                        pg_window_text(w, x, y+4, "..", edit_hex_mode?0x1E1E2E:0xCDD6F4);
                    }
                }
            } else {
                // empty beyond file
                if(abs==edit_cursor && edit_cursor==edit_length){
                    // cursor at EOF on this exact cell
                    uint32_t col = edit_length % HEXEDIT_BYTES_PER_ROW;
                    if(c==col){
                        pg_window_rect(w, (struct pg_rect){x, y+2, 16, 12}, 0x89B4FA);
                        pg_window_text(w, x, y+4, "  ", 0x1E1E2E);
                    }
                }
            }
        }
        // separator between hex and ascii
        pg_window_rect(w, (struct pg_rect){hex_x + HEXEDIT_BYTES_PER_ROW*3*8 + 8, y, 1, HEXEDIT_ROW_H}, 0x313244);

        // ascii
        for(uint32_t c=0;c<HEXEDIT_BYTES_PER_ROW;c++){
            uint32_t abs = offset + c;
            uint32_t x = ascii_x + c*8;
            if(abs < edit_length){
                char ch = (char)edit_buffer[abs];
                char s[2]; s[0]= (ch>=32 && ch<=126) ? ch : '.'; s[1]='\0';
                bool is_cursor = abs == edit_cursor;
                if(is_cursor && !edit_hex_mode){
                    pg_window_rect(w, (struct pg_rect){x, y+2, 8, 12}, 0xA6E3A1);
                    pg_window_text(w, x, y+4, s, 0x1E1E2E);
                } else if(is_cursor && edit_hex_mode){
                    pg_window_rect(w, (struct pg_rect){x, y+2, 8, 12}, 0x45475A);
                    pg_window_text(w, x, y+4, s, w->theme.text);
                } else {
                    uint32_t col = (edit_buffer[abs]>=32 && edit_buffer[abs]<=126) ? w->theme.text : w->theme.muted_text;
                    pg_window_text(w, x, y+4, s, col);
                }
            } else if(abs==edit_length && edit_cursor==edit_length && c==(edit_length%HEXEDIT_BYTES_PER_ROW)){
                pg_window_rect(w, (struct pg_rect){x, y+2, 8, 12}, !edit_hex_mode?0xA6E3A1:0x45475A);
                pg_window_text(w, x, y+4, " ", !edit_hex_mode?0x1E1E2E:w->theme.text);
            }
        }
    }
    // scroll indicator?
}

static bool do_draw(bool force){
    (void)force;
    if(hexedit_window_is_minimized(&edit_window)) return true;
    hexedit_window_begin(&edit_window);
    if(hexedit_window_is_minimized(&edit_window)){ hexedit_window_end(&edit_window); return true; }
    struct pg_window *w=&edit_window.gui;
    struct pg_rect client = hexedit_window_client(&edit_window);
    pg_window_clear(w, 0x1E1E2E);
    draw_toolbar();
    draw_header();
    draw_hex_rows();
    draw_status();
    // ensure window chrome redrawn already by pg_window_begin/end; but we need to flush
    hexedit_window_end(&edit_window);
    return true;
}

static void move_cursor(int32_t delta){
    int64_t n = (int64_t)edit_cursor + delta;
    if(n<0) n=0;
    if(n>(int64_t)edit_length) n=edit_length;
    edit_cursor=(uint32_t)n;
    edit_high_nibble=true;
    ensure_cursor_visible();
}

static void handle_hex_input(char ch){
    int v = hex_value(ch);
    if(v<0) return;
    if(edit_cursor == edit_length){
        if(!insert_byte(edit_cursor, (uint8_t)(v<<4))){
            set_status("No memory", true); return;
        }
        // we inserted byte with high nibble set, low 0
        // if we were at high nibble, next should be low nibble same byte
        edit_high_nibble=false;
        edit_dirty=true;
        set_status("", false);
        return;
    }
    if(edit_high_nibble){
        edit_buffer[edit_cursor] = (uint8_t)((v<<4) | (edit_buffer[edit_cursor] & 0x0F));
        edit_high_nibble=false;
    } else {
        edit_buffer[edit_cursor] = (uint8_t)((edit_buffer[edit_cursor] & 0xF0) | v);
        edit_cursor++;
        if(edit_cursor>edit_length) edit_cursor=edit_length;
        edit_high_nibble=true;
    }
    edit_dirty=true;
    ensure_cursor_visible();
    set_status("", false);
}

static void handle_ascii_input(char ch){
    if(ch<' ' || ch>'~') return;
    if(edit_cursor==edit_length){
        if(!insert_byte(edit_cursor, (uint8_t)ch)){ set_status("No memory",true); return; }
        edit_cursor++;
        edit_dirty=true;
        ensure_cursor_visible();
        return;
    }
    if(edit_insert_mode){
        if(!insert_byte(edit_cursor, (uint8_t)ch)){ set_status("No memory",true); return; }
        edit_cursor++;
    } else {
        edit_buffer[edit_cursor++]=(uint8_t)ch;
        if(edit_cursor>edit_length) edit_length=edit_cursor;
    }
    edit_dirty=true;
    ensure_cursor_visible();
    set_status("", false);
}

static uint32_t parse_hex_u32(const char *s, bool *ok){
    uint32_t v=0;
    *ok=false;
    if(!s || !*s) return 0;
    // skip 0x
    if(s[0]=='0' && (s[1]=='x' || s[1]=='X')) s+=2;
    if(!*s) return 0;
    while(*s){
        int hv=hex_value(*s);
        if(hv<0) return 0;
        if(v> (UINT32_MAX>>4)) return 0;
        v=(v<<4)|(uint32_t)hv;
        s++;
    }
    *ok=true;
    return v;
}

static void do_search(void){
    if(!prompt_buffer[0]){ set_status("Empty search", true); return; }
    uint32_t start = edit_cursor+1;
    if(start>edit_length) start=0;
    uint32_t plen = pc_strlen(prompt_buffer);
    for(uint32_t i=start; i+plen <= edit_length; i++){
        bool match=true;
        for(uint32_t j=0;j<plen;j++) if(edit_buffer[i+j] != (uint8_t)prompt_buffer[j]){ match=false; break; }
        if(match){ edit_cursor=i; edit_high_nibble=true; ensure_cursor_visible(); set_status("Found", false); return; }
    }
    // wrap search from 0
    for(uint32_t i=0; i<plen && i<start && i+plen <= edit_length; i++){
        bool match=true;
        for(uint32_t j=0;j<plen;j++) if(edit_buffer[i+j] != (uint8_t)prompt_buffer[j]){ match=false; break; }
        if(match){ edit_cursor=i; edit_high_nibble=true; ensure_cursor_visible(); set_status("Found (wrapped)", false); return; }
    }
    set_status("Not found", true);
}

static void enter_prompt(enum prompt_mode mode){
    prompt_mode=mode;
    prompt_buffer[0]='\0';
    set_status("", false);
}
static void exit_prompt(bool confirm){
    if(!confirm){ prompt_mode=PROMPT_NONE; set_status("Cancelled", false); return; }
    if(prompt_mode==PROMPT_GOTO){
        bool ok; uint32_t off=parse_hex_u32(prompt_buffer,&ok);
        if(!ok){ set_status("Invalid hex offset", true); prompt_mode=PROMPT_NONE; return; }
        if(off>edit_length) off=edit_length;
        edit_cursor=off; edit_high_nibble=true; ensure_cursor_visible();
        set_status("Goto done", false);
    } else if(prompt_mode==PROMPT_SEARCH){
        do_search();
    }
    prompt_mode=PROMPT_NONE;
}

static bool handle_prompt_key(int32_t key){
    if(key==27){ exit_prompt(false); return true; }
    if(key=='\r' || key=='\n'){ exit_prompt(true); return true; }
    uint32_t len=pc_strlen(prompt_buffer);
    if(key=='\b' || key==127){
        if(len) prompt_buffer[len-1]='\0';
        return true;
    }
    if(key>=' ' && key<='~' && len+1<sizeof(prompt_buffer)){
        prompt_buffer[len]=(char)key;
        prompt_buffer[len+1]='\0';
        return true;
    }
    return true;
}

static bool handle_special(uint8_t key){
    if(prompt_mode!=PROMPT_NONE) return false;
    uint32_t rows=visible_rows();
    switch(key){
        case KEYBOARD_SPECIAL_LEFT:
            if(edit_hex_mode && !edit_high_nibble){
                edit_high_nibble=true;
            } else {
                if(edit_cursor>0) edit_cursor--;
                edit_high_nibble=true;
            }
            ensure_cursor_visible(); return true;
        case KEYBOARD_SPECIAL_RIGHT:
            if(edit_hex_mode && edit_high_nibble && edit_cursor < edit_length){
                edit_high_nibble=false;
            } else {
                if(edit_cursor<edit_length) edit_cursor++;
                edit_high_nibble=true;
            }
            ensure_cursor_visible(); return true;
        case KEYBOARD_SPECIAL_UP:
            move_cursor(-(int32_t)HEXEDIT_BYTES_PER_ROW); return true;
        case KEYBOARD_SPECIAL_DOWN:
            move_cursor((int32_t)HEXEDIT_BYTES_PER_ROW); return true;
        case KEYBOARD_SPECIAL_PAGE_UP:
            {
                int32_t delta = -(int32_t)(rows * HEXEDIT_BYTES_PER_ROW);
                move_cursor(delta);
                // also page view
                // ensure already handles view
            }
            return true;
        case KEYBOARD_SPECIAL_PAGE_DOWN:
            move_cursor((int32_t)(rows*HEXEDIT_BYTES_PER_ROW)); return true;
        case KEYBOARD_SPECIAL_HOME:
            edit_cursor = (edit_cursor / HEXEDIT_BYTES_PER_ROW) * HEXEDIT_BYTES_PER_ROW;
            edit_high_nibble=true;
            ensure_cursor_visible(); return true;
        case KEYBOARD_SPECIAL_END:
            {
                uint32_t row_start = (edit_cursor / HEXEDIT_BYTES_PER_ROW) * HEXEDIT_BYTES_PER_ROW;
                uint32_t row_end = row_start + HEXEDIT_BYTES_PER_ROW;
                if(row_end>edit_length) row_end=edit_length;
                edit_cursor=row_end;
                // if at end of row but not at file end, move to last byte of row
                if(edit_cursor>row_start && edit_cursor==row_end && row_end!=edit_length) edit_cursor=row_end-1;
                edit_high_nibble=true;
                ensure_cursor_visible();
            }
            return true;
        case KEYBOARD_SPECIAL_DELETE:
            if(edit_cursor < edit_length){
                delete_byte(edit_cursor);
                edit_dirty=true;
                set_status("", false);
            }
            return true;
        case KEYBOARD_SPECIAL_F1:
            edit_insert_mode=!edit_insert_mode;
            set_status(edit_insert_mode?"Insert mode":"Overwrite mode", false);
            return true;
        default: return false;
    }
}

static bool handle_mouse(const struct pg_event *ev){
    if(!ev || ev->type!=PG_EVENT_MOUSE_UP || ev->button!=1) return false;
    struct pg_rect client = hexedit_window_client(&edit_window);
    int32_t mx = ev->x - (int32_t)client.x;
    int32_t my = ev->y - (int32_t)client.y;
    uint32_t top = HEXEDIT_TOOLBAR_H + HEXEDIT_HEADER_H;
    if(my < (int32_t)top || my >= (int32_t)(client.height - HEXEDIT_STATUS_H)) return false;
    uint32_t row = (uint32_t)(my - (int32_t)top) / HEXEDIT_ROW_H;
    uint32_t offset = edit_view_offset + row*HEXEDIT_BYTES_PER_ROW;
    if(offset > edit_length) return false;
    uint32_t hex_x = 8 + HEXEDIT_OFFSET_CHARS*8 + 8;
    uint32_t ascii_x = hex_x + HEXEDIT_BYTES_PER_ROW*3*8 + 4 + 16;
    if(mx >= (int32_t)hex_x && mx < (int32_t)(hex_x + HEXEDIT_BYTES_PER_ROW*3*8 + 4)){
        // hex pane
        int32_t rel = mx - (int32_t)hex_x;
        // handle gap after 8 bytes
        uint32_t col;
        if(rel < (int32_t)(8*3*8)){
            col = (uint32_t)rel / (3*8);
        } else if(rel < (int32_t)(8*3*8 +4)){
            return false;
        } else {
            col = 8 + (uint32_t)(rel - (8*3*8+4)) / (3*8);
        }
        if(col >= HEXEDIT_BYTES_PER_ROW) return false;
        uint32_t target = offset + col;
        if(target > edit_length) target = edit_length;
        edit_cursor = target;
        // nibble from x offset within cell
        uint32_t cell_x = hex_x + col*3*8 + (col>=8?4:0);
        int32_t within = mx - (int32_t)cell_x;
        if(within >= 8){
            edit_high_nibble=false;
        } else {
            edit_high_nibble=true;
        }
        edit_hex_mode=true;
        ensure_cursor_visible();
        return true;
    } else if(mx >= (int32_t)ascii_x && mx < (int32_t)(ascii_x + HEXEDIT_BYTES_PER_ROW*8)){
        uint32_t col = (uint32_t)(mx - (int32_t)ascii_x) / 8;
        if(col >= HEXEDIT_BYTES_PER_ROW) return false;
        uint32_t target = offset + col;
        if(target > edit_length) target = edit_length;
        edit_cursor = target;
        edit_hex_mode=false;
        edit_high_nibble=true;
        ensure_cursor_visible();
        return true;
    }
    return false;
}

int hexedit_run(const char *path){
    edit_path = path;
    edit_buffer=0; edit_length=0; edit_capacity=0;
    edit_cursor=0; edit_high_nibble=true; edit_hex_mode=true; edit_insert_mode=false;
    edit_dirty=false; edit_view_offset=0; prompt_mode=PROMPT_NONE;
    status_msg[0]='\0'; prompt_buffer[0]='\0';
    if(!reserve_buffer(HEXEDIT_INITIAL_CAPACITY)) return 1;
    int loaded = load_file();
    if(loaded<0){ set_status("Failed to load", true); return 1; }
    if(loaded==0) set_status("New file", false); else set_status("", false);
    if(!hexedit_window_init(&edit_window)) return 1;
    if(!do_draw(true)) return 1;
    bool exit_armed=false;
    for(;;){
        struct pg_event ev;
        if(!hexedit_window_poll_event(&edit_window,&ev)){
            pc_sleep(16); continue;
        }
        if(ev.type==PG_EVENT_CLOSE){ hexedit_window_shutdown(&edit_window); return 0; }
        if(ev.type==PG_EVENT_MOVE || ev.type==PG_EVENT_MINIMIZE || ev.type==PG_EVENT_FOCUS || ev.type==PG_EVENT_REPAINT){
            (void)do_draw(true); continue;
        }
        if(ev.type==PG_EVENT_MOUSE_DOWN || ev.type==PG_EVENT_MOUSE_UP || ev.type==PG_EVENT_MOUSE_MOVE){
            if(handle_mouse(&ev)) (void)do_draw(true);
            continue;
        }
        if(ev.type==PG_EVENT_SPECIAL_KEY){
            if(handle_special((uint8_t)ev.key)){ exit_armed=false; (void)do_draw(true); }
            continue;
        }
        if(ev.type!=PG_EVENT_KEY || hexedit_window_is_minimized(&edit_window)) continue;
        char ch=(char)ev.key;
        // prompt handling first
        if(prompt_mode!=PROMPT_NONE){
            handle_prompt_key(ev.key);
            (void)do_draw(true);
            continue;
        }
        // Ctrl keys
        if(ch==19){ // Ctrl+S save
            exit_armed=false;
            (void)do_draw(true);
            if(save_file()) set_status("Saved", false); else set_status("Save failed", true);
            (void)do_draw(true);
            continue;
        }
        if(ch==24){ // Ctrl+X exit
            if(edit_dirty && !exit_armed){ exit_armed=true; set_status("Unsaved Ctrl+X again to discard", true); (void)do_draw(true); continue; }
            hexedit_window_shutdown(&edit_window); return 0;
        }
        if(ch==7){ // Ctrl+G goto
            exit_armed=false; enter_prompt(PROMPT_GOTO); (void)do_draw(true); continue;
        }
        if(ch==6){ // Ctrl+F find
            exit_armed=false; enter_prompt(PROMPT_SEARCH); (void)do_draw(true); continue;
        }
        if(ch=='\t'){ edit_hex_mode=!edit_hex_mode; edit_high_nibble=true; (void)do_draw(true); continue; }
        if(ch=='\r') ch='\n';
        if(ch=='\b' || ch==127){
            if(edit_cursor){
                edit_cursor--;
                delete_byte(edit_cursor);
                edit_dirty=true;
                edit_high_nibble=true;
                ensure_cursor_visible();
                set_status("", false);
                (void)do_draw(true);
            }
            continue;
        }
        if(ch=='\n'){ // maybe ignore or insert? In hex edit, enter could move to next row
            move_cursor(HEXEDIT_BYTES_PER_ROW - (edit_cursor % HEXEDIT_BYTES_PER_ROW));
            (void)do_draw(true);
            continue;
        }
        if(edit_hex_mode){
            if(hex_value(ch)>=0){
                handle_hex_input(ch);
                (void)do_draw(true);
            }
        } else {
            if(ch>=' ' && ch<='~'){
                handle_ascii_input(ch);
                (void)do_draw(true);
            }
        }
    }
}
