#include "../../../libgui/include/puregui.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include "../../../drivers/input/keyboard.h"

#define BOARD_W 10
#define BOARD_H 20

static int8_t board[BOARD_H][BOARD_W];
static int32_t piece_type;
static int32_t piece_rotation;
static int32_t piece_x;
static int32_t piece_y;
static int32_t next_type;
static uint32_t score;
static uint32_t lines;
static uint32_t level;
static bool game_over;
static bool paused;
static uint32_t random_state=0x1234ABCD;
static uint32_t drop_interval_ms=600;
static uint32_t drop_timer;

// dynamic layout for huge window
static uint32_t g_cell = 28;
static uint32_t g_preview_cell = 18;
static uint32_t g_board_x = 20;
static uint32_t g_board_y = 20;
static uint32_t g_side_x = 320;
static uint32_t g_side_y = 20;
static uint32_t g_side_w = 280;
static uint32_t g_win_w = 640;
static uint32_t g_win_h = 620;

static uint32_t piece_colors[7]={
    0x89B4FA,
    0xF9E2AF,
    0xCBA6F7,
    0xA6E3A1,
    0xF38BA8,
    0x89DCEB,
    0xFAB387
};

static const int8_t SHAPES[7][4][4][2]={
    {
        {{-1,0},{0,0},{1,0},{2,0}},
        {{1,-1},{1,0},{1,1},{1,2}},
        {{-1,1},{0,1},{1,1},{2,1}},
        {{0,-1},{0,0},{0,1},{0,2}}
    },
    {
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}}
    },
    {
        {{-1,0},{0,0},{1,0},{0,1}},
        {{0,-1},{0,0},{0,1},{1,0}},
        {{-1,1},{0,1},{1,1},{0,0}},
        {{0,-1},{0,0},{0,1},{-1,0}}
    },
    {
        {{0,0},{1,0},{-1,1},{0,1}},
        {{0,-1},{0,0},{1,0},{1,1}},
        {{0,0},{1,0},{-1,1},{0,1}},
        {{0,-1},{0,0},{1,0},{1,1}}
    },
    {
        {{-1,0},{0,0},{0,1},{1,1}},
        {{1,-1},{0,0},{1,0},{0,1}},
        {{-1,0},{0,0},{0,1},{1,1}},
        {{1,-1},{0,0},{1,0},{0,1}}
    },
    {
        {{-1,0},{-1,1},{0,1},{1,1}},
        {{0,-1},{1,-1},{0,0},{0,1}},
        {{-1,1},{0,1},{1,1},{1,0}},
        {{0,-1},{0,0},{0,1},{-1,1}}
    },
    {
        {{1,0},{-1,1},{0,1},{1,1}},
        {{0,-1},{0,0},{0,1},{1,1}},
        {{-1,0},{-1,1},{0,1},{1,1}},
        {{-1,-1},{0,-1},{0,0},{0,1}}
    }
};

static void compute_layout(void){
    struct pc_display_info info;
    uint32_t disp_w=1024, disp_h=768;
    bool have_info=false;
    if(pc_display_get_info(&info) && info.available && info.width>=640 && info.height>=480){
        disp_w=info.width;
        disp_h=info.height;
        have_info=true;
    }
    // candidates from huge to medium
    const uint32_t cand_cell[5]={30,28,26,24,20};
    const uint32_t cand_prev[5]={20,18,16,14,12};
    const uint32_t cand_side_w[5]={280,280,260,260,260};
    // try biggest that fits
    for(uint32_t i=0;i<5;i++){
        uint32_t cell=cand_cell[i];
        uint32_t prev=cand_prev[i];
        uint32_t side_w=cand_side_w[i];
        uint32_t board_px_w=BOARD_W*cell;
        uint32_t board_px_h=BOARD_H*cell;
        uint32_t client_w=20 + board_px_w + 20 + side_w + 20;
        uint32_t client_h=20 + board_px_h + 20;
        uint32_t frame_w=client_w + PG_WINDOW_BORDER*2;
        uint32_t frame_h=client_h + PG_TITLEBAR_HEIGHT + PG_WINDOW_BORDER*2;
        uint32_t max_w = have_info ? (disp_w>40?disp_w-40:disp_w) : 800;
        uint32_t max_h = have_info ? (disp_h>60?disp_h-60:disp_h) : 600;
        if(frame_w<=max_w && frame_h<=max_h){
            g_cell=cell;
            g_preview_cell=prev;
            g_side_w=side_w;
            g_board_x=20;
            g_board_y=20;
            g_side_x=g_board_x + board_px_w + 20;
            g_side_y=g_board_y;
            g_win_w=frame_w;
            g_win_h=frame_h;
            return;
        }
    }
    // fallback absolute minimum (huge still but smallest)
    g_cell=20;
    g_preview_cell=14;
    g_side_w=260;
    g_board_x=18;
    g_board_y=18;
    g_side_x=g_board_x + BOARD_W*g_cell + 18;
    g_side_y=g_board_y;
    uint32_t client_w=18 + BOARD_W*g_cell + 18 + g_side_w + 18;
    uint32_t client_h=18 + BOARD_H*g_cell + 18;
    g_win_w=client_w+4;
    g_win_h=client_h+30;
    // clamp to display if still too big
    if(have_info){
        if(g_win_w>disp_w) g_win_w=disp_w>20?disp_w-20:520;
        if(g_win_h>disp_h) g_win_h=disp_h>20?disp_h-20:560;
    }
}

static uint32_t random_value(void){
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void update_level_speed(void){
    level = lines / 10 + 1;
    if(level>20) level=20;
    if(level<=10) drop_interval_ms = 600 - (level-1)*45;
    else drop_interval_ms = 150 - (level-10)*5;
    if(drop_interval_ms<80) drop_interval_ms=80;
}

static bool check_collision(int32_t type,int32_t rotation,int32_t x,int32_t y){
    for(int32_t i=0;i<4;i++){
        int32_t bx = x + SHAPES[type][rotation][i][0];
        int32_t by = y + SHAPES[type][rotation][i][1];
        if(bx<0 || bx>=BOARD_W) return true;
        if(by>=BOARD_H) return true;
        if(by<0) continue;
        if(board[by][bx]!=-1) return true;
    }
    return false;
}

static void lock_piece(void){
    for(int32_t i=0;i<4;i++){
        int32_t bx = piece_x + SHAPES[piece_type][piece_rotation][i][0];
        int32_t by = piece_y + SHAPES[piece_type][piece_rotation][i][1];
        if(by>=0 && by<BOARD_H && bx>=0 && bx<BOARD_W){
            board[by][bx]=(int8_t)piece_type;
        }
    }
}

static int32_t clear_lines(void){
    int32_t cleared=0;
    for(int32_t y=BOARD_H-1;y>=0;y--){
        bool full=true;
        for(int32_t x=0;x<BOARD_W;x++) if(board[y][x]==-1){ full=false; break; }
        if(full){
            cleared++;
            for(int32_t ty=y;ty>0;ty--){
                for(int32_t x=0;x<BOARD_W;x++) board[ty][x]=board[ty-1][x];
            }
            for(int32_t x=0;x<BOARD_W;x++) board[0][x]=-1;
            y++;
        }
    }
    return cleared;
}

static void spawn_piece(void){
    piece_type = next_type;
    next_type = (int32_t)(random_value()%7);
    piece_rotation=0;
    piece_x=4;
    piece_y=1;
    if(check_collision(piece_type,piece_rotation,piece_x,piece_y)){
        game_over=true;
    }
}

static void reset_game(void){
    for(int32_t y=0;y<BOARD_H;y++) for(int32_t x=0;x<BOARD_W;x++) board[y][x]=-1;
    score=0;
    lines=0;
    level=1;
    drop_interval_ms=600;
    game_over=false;
    paused=false;
    drop_timer=0;
    next_type=(int32_t)(random_value()%7);
    spawn_piece();
    update_level_speed();
}

static void add_score(int32_t lines_cleared){
    if(lines_cleared<=0) return;
    static const uint32_t table[5]={0,100,300,500,800};
    if(lines_cleared>4) lines_cleared=4;
    score += table[lines_cleared]*level;
    lines += (uint32_t)lines_cleared;
    update_level_speed();
}

static bool try_move(int32_t dx,int32_t dy){
    if(game_over || paused) return false;
    if(!check_collision(piece_type,piece_rotation,piece_x+dx,piece_y+dy)){
        piece_x+=dx;
        piece_y+=dy;
        return true;
    }
    return false;
}

static bool try_rotate(void){
    if(game_over || paused) return false;
    int32_t new_rot=(piece_rotation+1)%4;
    if(!check_collision(piece_type,new_rot,piece_x,piece_y)){
        piece_rotation=new_rot;
        return true;
    }
    if(!check_collision(piece_type,new_rot,piece_x-1,piece_y)){
        piece_x-=1;
        piece_rotation=new_rot;
        return true;
    }
    if(!check_collision(piece_type,new_rot,piece_x+1,piece_y)){
        piece_x+=1;
        piece_rotation=new_rot;
        return true;
    }
    if(!check_collision(piece_type,new_rot,piece_x,piece_y-1)){
        piece_y-=1;
        piece_rotation=new_rot;
        return true;
    }
    return false;
}

static void hard_drop(void){
    if(game_over || paused) return;
    int32_t drop=0;
    while(!check_collision(piece_type,piece_rotation,piece_x,piece_y+1)){
        piece_y++;
        drop++;
    }
    score += (uint32_t)(drop*2);
}

static char *append_text(char *dst,const char *src){
    while(*src) *dst++=*src++;
    *dst='\0';
    return dst;
}
static char *append_u32(char *dst,uint32_t v){
    char rev[12];
    uint32_t len=0;
    if(v==0) rev[len++]='0';
    while(v){ rev[len++]=(char)('0'+v%10); v/=10; }
    while(len) *dst++=rev[--len];
    *dst='\0';
    return dst;
}

static void draw_board(struct pg_window *window){
    // outer border
    pg_window_rect(window,(struct pg_rect){g_board_x-2,g_board_y-2,BOARD_W*g_cell+4,BOARD_H*g_cell+4},window->theme.border);
    pg_window_rect(window,(struct pg_rect){g_board_x,g_board_y,BOARD_W*g_cell,BOARD_H*g_cell},0x11111B);
    // subtle grid for huge cells (helps orientation without being a hint)
    if(g_cell>=24){
        // vertical lines
        for(uint32_t x=1;x<BOARD_W;x++){
            pg_window_rect(window,(struct pg_rect){g_board_x + x*g_cell, g_board_y, 1, BOARD_H*g_cell},0x1E1E2E);
        }
        for(uint32_t y=1;y<BOARD_H;y++){
            pg_window_rect(window,(struct pg_rect){g_board_x, g_board_y + y*g_cell, BOARD_W*g_cell, 1},0x1E1E2E);
        }
    }
    // locked blocks
    for(int32_t y=0;y<BOARD_H;y++){
        for(int32_t x=0;x<BOARD_W;x++){
            int8_t cell=board[y][x];
            if(cell==-1) continue;
            uint32_t color=piece_colors[cell];
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)x*g_cell+1, g_board_y + (uint32_t)y*g_cell+1, g_cell-2, g_cell-2},color);
            // top highlight
            uint32_t hl_h = g_cell>=28?4:3;
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)x*g_cell+1, g_board_y + (uint32_t)y*g_cell+1, g_cell-2, hl_h},color+0x222222);
            // bottom shadow
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)x*g_cell+1, g_board_y + (uint32_t)(y+1)*g_cell -3, g_cell-2, 2},0x11111B);
        }
    }
    if(!game_over){
        // current piece only, no ghost/hint
        for(int32_t i=0;i<4;i++){
            int32_t bx = piece_x + SHAPES[piece_type][piece_rotation][i][0];
            int32_t by = piece_y + SHAPES[piece_type][piece_rotation][i][1];
            if(by<0 || by>=BOARD_H || bx<0 || bx>=BOARD_W) continue;
            uint32_t color=piece_colors[piece_type];
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)bx*g_cell+1, g_board_y + (uint32_t)by*g_cell+1, g_cell-2, g_cell-2},color);
            uint32_t hl_h = g_cell>=28?4:3;
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)bx*g_cell+1, g_board_y + (uint32_t)by*g_cell+1, g_cell-2, hl_h},color+0x222222);
        }
    }
}

static void draw_side(struct pg_window *window){
    uint32_t sx=g_side_x;
    uint32_t sy=g_side_y;

    uint32_t preview_box = 4*g_preview_cell + 12;
    // ensure box fits side width
    if(preview_box > g_side_w - 20) preview_box = g_side_w - 20;
    uint32_t panel_h = preview_box + 98; // 26 title + 6 gap + 12 border + 54 stats
    // side main panel
    pg_window_rect(window,(struct pg_rect){sx,sy,g_side_w,panel_h},window->theme.titlebar);
    pg_window_rect(window,(struct pg_rect){sx,sy,g_side_w,1},window->theme.border);
    pg_window_rect(window,(struct pg_rect){sx,sy,1,panel_h},window->theme.border);
    pg_window_rect(window,(struct pg_rect){sx+g_side_w-1,sy,1,panel_h},window->theme.border);
    pg_window_rect(window,(struct pg_rect){sx,sy+panel_h-1,g_side_w,1},window->theme.border);

    pg_window_text(window,sx+12,sy+8,"NEXT",window->theme.text);

    uint32_t bg_w = preview_box;
    uint32_t bg_h = preview_box;
    uint32_t bg_x = sx + (g_side_w - bg_w)/2;
    uint32_t bg_y = sy + 26;

    // border for preview
    pg_window_rect(window,(struct pg_rect){bg_x-2,bg_y-2,bg_w+4,bg_h+4},window->theme.border);
    pg_window_rect(window,(struct pg_rect){bg_x,bg_y,bg_w,bg_h},0x11111B);

    // calculate centered offsets for next piece
    int32_t minX=10, maxX=-10, minY=10, maxY=-10;
    for(int32_t i=0;i<4;i++){
        int32_t bx=SHAPES[next_type][0][i][0];
        int32_t by=SHAPES[next_type][0][i][1];
        if(bx<minX) minX=bx;
        if(bx>maxX) maxX=bx;
        if(by<minY) minY=by;
        if(by>maxY) maxY=by;
    }
    int32_t w = maxX - minX + 1;
    int32_t h = maxY - minY + 1;
    int32_t offX = (4 - w)/2 - minX;
    int32_t offY = (4 - h)/2 - minY;
    uint32_t inner_x = bg_x + (bg_w - 4*g_preview_cell)/2;
    uint32_t inner_y = bg_y + (bg_h - 4*g_preview_cell)/2;
    uint32_t color = piece_colors[next_type];
    for(int32_t i=0;i<4;i++){
        int32_t bx=SHAPES[next_type][0][i][0];
        int32_t by=SHAPES[next_type][0][i][1];
        int32_t px=bx+offX;
        int32_t py=by+offY;
        if(px<0 || px>=4 || py<0 || py>=4) continue;
        pg_window_rect(window,(struct pg_rect){inner_x + (uint32_t)px*g_preview_cell+1, inner_y + (uint32_t)py*g_preview_cell+1, g_preview_cell-2, g_preview_cell-2},color);
        // highlight
        pg_window_rect(window,(struct pg_rect){inner_x + (uint32_t)px*g_preview_cell+1, inner_y + (uint32_t)py*g_preview_cell+1, g_preview_cell-2, 2},color+0x222222);
    }

    // stats below preview
    uint32_t stats_y = bg_y + bg_h + 14;
    {
        char line[32];
        char *pos=line;
        pos=append_text(pos,"Score: "); pos=append_u32(pos,score);
        pg_window_text(window,sx+14,stats_y,line,window->theme.text);
    }
    {
        char line[32];
        char *pos=line;
        pos=append_text(pos,"Lines: "); pos=append_u32(pos,lines);
        pg_window_text(window,sx+14,stats_y+18,line,window->theme.text);
    }
    {
        char line[32];
        char *pos=line;
        pos=append_text(pos,"Level: "); pos=append_u32(pos,level);
        pg_window_text(window,sx+14,stats_y+36,line,window->theme.text);
    }

    // no hints at all - only score
    (void)stats_y;

    if(paused && !game_over){
        uint32_t py = sy + panel_h + 12;
        pg_window_rect(window,(struct pg_rect){sx,py,g_side_w,28},0xF9E2AF);
        pg_window_rect(window,(struct pg_rect){sx,py,g_side_w,28},0xF9E2AF);
        // centered text approx
        uint32_t tx = sx + (g_side_w - 48)/2;
        pg_window_text(window,tx,py+10,"PAUSED",0x1E1E2E);
    }
    if(game_over){
        uint32_t py = sy + panel_h + 12;
        // if not paused, py same; if paused overdraw?
        if(paused) py+=36;
        pg_window_rect(window,(struct pg_rect){sx,py,g_side_w,28},0xF38BA8);
        uint32_t tx = sx + (g_side_w - 72)/2;
        pg_window_text(window,tx,py+10,"GAME OVER",0x1E1E2E);
        pg_window_text(window,sx+12,py+34,"Press R to restart",window->theme.danger);
    }
}

static void redraw(struct pg_window *window){
    pg_window_begin(window);
    draw_board(window);
    draw_side(window);
    pg_window_end(window);
}

static void handle_input(struct pg_window *window, struct pg_event *event){
    if(event->type==PG_EVENT_CLOSE){
        pg_window_close(window);
        return;
    }
    if(event->type==PG_EVENT_KEY){
        int32_t k=event->key;
        if(k=='q' || k=='Q'){
            pg_window_close(window);
            return;
        }
        if(game_over){
            if(k=='r' || k=='R'){
                reset_game();
                redraw(window);
            }
            return;
        }
        if(k=='p' || k=='P'){
            paused=!paused;
            redraw(window);
            return;
        }
        if(paused) return;
        if(k=='a' || k=='A'){
            if(try_move(-1,0)) redraw(window);
        } else if(k=='d' || k=='D'){
            if(try_move(1,0)) redraw(window);
        } else if(k=='s' || k=='S'){
            if(try_move(0,1)){
                score+=1;
                redraw(window);
            } else {
                lock_piece();
                int32_t cleared=clear_lines();
                add_score(cleared);
                spawn_piece();
                drop_timer=0;
                redraw(window);
            }
        } else if(k=='w' || k=='W'){
            if(try_rotate()) redraw(window);
        } else if(k==' '){
            hard_drop();
            lock_piece();
            int32_t cleared=clear_lines();
            add_score(cleared);
            spawn_piece();
            drop_timer=0;
            redraw(window);
        } else if(k=='r' || k=='R'){
            reset_game();
            redraw(window);
        }
    } else if(event->type==PG_EVENT_SPECIAL_KEY){
        if(game_over || paused) return;
        if(event->key==KEYBOARD_SPECIAL_LEFT){
            if(try_move(-1,0)) redraw(window);
        } else if(event->key==KEYBOARD_SPECIAL_RIGHT){
            if(try_move(1,0)) redraw(window);
        } else if(event->key==KEYBOARD_SPECIAL_DOWN){
            if(try_move(0,1)){
                score+=1;
                redraw(window);
            } else {
                lock_piece();
                int32_t cleared=clear_lines();
                add_score(cleared);
                spawn_piece();
                drop_timer=0;
                redraw(window);
            }
        } else if(event->key==KEYBOARD_SPECIAL_UP){
            if(try_rotate()) redraw(window);
        }
    }
}

static int tetris_main(void){
    compute_layout();
    struct pg_window window;
    // try to open with computed size, fallback to smaller if fails
    bool opened=false;
    const uint32_t try_cells[5]={30,28,26,24,20};
    // if computed already fails, iterate
    for(int32_t attempt=0;attempt<6;attempt++){
        if(pg_window_center(&window,"Tetris",g_win_w,g_win_h)){
            opened=true;
            break;
        }
        // shrink
        if(attempt<5){
            uint32_t cell=try_cells[attempt];
            // recompute for this cell if not already
            if(cell==g_cell) continue;
            uint32_t side_w = (cell>=28?280:260);
            uint32_t board_px_w=BOARD_W*cell;
            uint32_t board_px_h=BOARD_H*cell;
            uint32_t client_w=20 + board_px_w + 20 + side_w + 20;
            uint32_t client_h=20 + board_px_h + 20;
            g_cell=cell;
            g_preview_cell=(cell>=30?20:(cell>=28?18:(cell>=26?16:(cell>=24?14:12))));
            g_side_w=side_w;
            g_board_x=20;
            g_board_y=20;
            g_side_x=g_board_x + board_px_w + 20;
            g_side_y=20;
            g_win_w=client_w+4;
            g_win_h=client_h+30;
        } else {
            // ultimate fallback 520x560
            g_cell=20; g_preview_cell=14; g_board_x=18; g_board_y=18; g_side_x=238; g_side_y=18; g_side_w=260; g_win_w=520; g_win_h=560;
            if(pg_window_center(&window,"Tetris",g_win_w,g_win_h)){ opened=true; break;}
        }
    }
    if(!opened) return 1;
    reset_game();
    redraw(&window);
    while(pg_window_is_open(&window)){
        struct pg_event event;
        bool has_event=pg_window_poll_event(&window,&event);
        if(has_event){
            if(event.type==PG_EVENT_REPAINT || event.type==PG_EVENT_MOVE || event.type==PG_EVENT_FOCUS){
                redraw(&window);
            } else {
                handle_input(&window,&event);
            }
            if(!pg_window_is_open(&window)) break;
        } else {
            pc_sleep(16);
        }
        if(pg_window_is_minimized(&window)) continue;
        if(game_over || paused) continue;
        drop_timer+=16;
        if(drop_timer>=drop_interval_ms){
            drop_timer=0;
            if(!try_move(0,1)){
                lock_piece();
                int32_t cleared=clear_lines();
                add_score(cleared);
                spawn_piece();
                redraw(&window);
                if(game_over) redraw(&window);
            } else {
                redraw(&window);
            }
        }
    }
    pg_window_close(&window);
    return 0;
}

void _start(void){
    pc_exit(tetris_main());
}
