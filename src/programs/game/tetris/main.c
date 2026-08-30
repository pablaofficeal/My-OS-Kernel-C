#include "../../../libgui/include/puregui.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include "../../../drivers/input/keyboard.h"

#define BOARD_W 10
#define BOARD_H 20
#define CELL 20
#define BOARD_X 18
#define BOARD_Y 18
#define SIDE_X 238
#define SIDE_Y 18
#define PREVIEW_CELL 14

#define TETRIS_WIDTH 520
#define TETRIS_HEIGHT 560

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

static uint32_t piece_colors[7]={
    0x89B4FA, // I cyan/blue
    0xF9E2AF, // O yellow
    0xCBA6F7, // T purple
    0xA6E3A1, // S green
    0xF38BA8, // Z red
    0x89DCEB, // J sky
    0xFAB387  // L peach
};

// 7 types x 4 rotations x 4 blocks x 2 coordinates
static const int8_t SHAPES[7][4][4][2]={
    // I
    {
        {{-1,0},{0,0},{1,0},{2,0}},
        {{1,-1},{1,0},{1,1},{1,2}},
        {{-1,1},{0,1},{1,1},{2,1}},
        {{0,-1},{0,0},{0,1},{0,2}}
    },
    // O
    {
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}}
    },
    // T
    {
        {{-1,0},{0,0},{1,0},{0,1}},
        {{0,-1},{0,0},{0,1},{1,0}},
        {{-1,1},{0,1},{1,1},{0,0}},
        {{0,-1},{0,0},{0,1},{-1,0}}
    },
    // S
    {
        {{0,0},{1,0},{-1,1},{0,1}},
        {{0,-1},{0,0},{1,0},{1,1}},
        {{0,0},{1,0},{-1,1},{0,1}},
        {{0,-1},{0,0},{1,0},{1,1}}
    },
    // Z
    {
        {{-1,0},{0,0},{0,1},{1,1}},
        {{1,-1},{0,0},{1,0},{0,1}},
        {{-1,0},{0,0},{0,1},{1,1}},
        {{1,-1},{0,0},{1,0},{0,1}}
    },
    // J
    {
        {{-1,0},{-1,1},{0,1},{1,1}},
        {{0,-1},{1,-1},{0,0},{0,1}},
        {{-1,1},{0,1},{1,1},{1,0}},
        {{0,-1},{0,0},{0,1},{-1,1}}
    },
    // L
    {
        {{1,0},{-1,1},{0,1},{1,1}},
        {{0,-1},{0,0},{0,1},{1,1}},
        {{-1,0},{-1,1},{0,1},{1,1}},
        {{-1,-1},{0,-1},{0,0},{0,1}}
    }
};

static uint32_t random_value(void){
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void update_level_speed(void){
    level = lines / 10 + 1;
    if(level>20) level=20;
    // interval from 600 down to 100
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
            y++; // recheck same row
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
    // wall kicks simple left/right
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
    // board background
    pg_window_rect(window,(struct pg_rect){BOARD_X-2,BOARD_Y-2,BOARD_W*CELL+4,BOARD_H*CELL+4},window->theme.border);
    pg_window_rect(window,(struct pg_rect){BOARD_X,BOARD_Y,BOARD_W*CELL,BOARD_H*CELL},0x11111B);
    // grid lines subtle
    for(int32_t y=0;y<BOARD_H;y++){
        for(int32_t x=0;x<BOARD_W;x++){
            uint32_t color;
            int8_t cell=board[y][x];
            if(cell!=-1){
                color=piece_colors[cell];
            } else {
                // empty empty just leave background, optional grid dot
                continue;
            }
            pg_window_rect(window,(struct pg_rect){(uint32_t)(BOARD_X+x*CELL+1),(uint32_t)(BOARD_Y+y*CELL+1),CELL-2,CELL-2},color);
            // highlight top
            pg_window_rect(window,(struct pg_rect){(uint32_t)(BOARD_X+x*CELL+1),(uint32_t)(BOARD_Y+y*CELL+1),CELL-2,3},color+0x222222);
        }
    }
    if(!game_over){
        // draw ghost
        int32_t ghost_y=piece_y;
        while(!check_collision(piece_type,piece_rotation,piece_x,ghost_y+1)) ghost_y++;
        if(ghost_y!=piece_y){
            for(int32_t i=0;i<4;i++){
                int32_t bx = piece_x + SHAPES[piece_type][piece_rotation][i][0];
                int32_t by = ghost_y + SHAPES[piece_type][piece_rotation][i][1];
                if(by<0 || by>=BOARD_H || bx<0 || bx>=BOARD_W) continue;
                if(board[by][bx]!=-1) continue;
                pg_window_rect(window,(struct pg_rect){(uint32_t)(BOARD_X+bx*CELL+2),(uint32_t)(BOARD_Y+by*CELL+2),CELL-4,CELL-4},0x45475A);
            }
        }
        // draw current piece
        for(int32_t i=0;i<4;i++){
            int32_t bx = piece_x + SHAPES[piece_type][piece_rotation][i][0];
            int32_t by = piece_y + SHAPES[piece_type][piece_rotation][i][1];
            if(by<0 || by>=BOARD_H || bx<0 || bx>=BOARD_W) continue;
            uint32_t color=piece_colors[piece_type];
            pg_window_rect(window,(struct pg_rect){(uint32_t)(BOARD_X+bx*CELL+1),(uint32_t)(BOARD_Y+by*CELL+1),CELL-2,CELL-2},color);
        }
    }
}

static void draw_side(struct pg_window *window){
    uint32_t sx=SIDE_X;
    uint32_t sy=SIDE_Y;
    // panel for next
    pg_window_rect(window,(struct pg_rect){sx,sy,260,110},window->theme.titlebar);
    pg_window_text(window,sx+10,sy+8,"Next:",window->theme.text);
    // next preview background
    pg_window_rect(window,(struct pg_rect){sx+90,sy+8,80,80},0x1E1E2E);
    // draw next piece centered 4x4
    int32_t ox=0,oy=0;
    // compute bounding to center? simple offset 28,28 inside 80x80 with preview cell 14
    // preview uses cell 14, board 4*14=56, centered at 12 px offset
    uint32_t preview_x=sx+90+12;
    uint32_t preview_y=sy+8+12;
    for(int32_t i=0;i<4;i++){
        int32_t bx=SHAPES[next_type][0][i][0];
        int32_t by=SHAPES[next_type][0][i][1];
        // offset to make visible inside 0..3 range, shift by 1
        int32_t px=bx+1;
        int32_t py=by+1;
        uint32_t color=piece_colors[next_type];
        pg_window_rect(window,(struct pg_rect){preview_x+(uint32_t)(px*PREVIEW_CELL),preview_y+(uint32_t)(py*PREVIEW_CELL),PREVIEW_CELL-2,PREVIEW_CELL-2},color);
    }

    // Score
    {
        char line[32];
        char *pos=line;
        pos=append_text(pos,"Score: "); pos=append_u32(pos,score);
        pg_window_text(window,sx+10,sy+34,line,window->theme.text);
    }
    {
        char line[32];
        char *pos=line;
        pos=append_text(pos,"Lines: "); pos=append_u32(pos,lines);
        pg_window_text(window,sx+10,sy+50,line,window->theme.text);
    }
    {
        char line[32];
        char *pos=line;
        pos=append_text(pos,"Level: "); pos=append_u32(pos,level);
        pg_window_text(window,sx+10,sy+66,line,window->theme.text);
    }
    // Controls
    uint32_t cy=sy+115;
    pg_window_rect(window,(struct pg_rect){sx,cy,260,160},window->theme.titlebar);
    pg_window_text(window,sx+10,cy+8,"Controls:",window->theme.text);
    pg_window_text(window,sx+10,cy+24,"Arrows / WASD - move",window->theme.muted_text);
    pg_window_text(window,sx+10,cy+38,"Up / W - rotate",window->theme.muted_text);
    pg_window_text(window,sx+10,cy+52,"Down / S - soft drop",window->theme.muted_text);
    pg_window_text(window,sx+10,cy+66,"Space - hard drop",window->theme.muted_text);
    pg_window_text(window,sx+10,cy+80,"P - pause  R - restart",window->theme.muted_text);
    pg_window_text(window,sx+10,cy+94,"Q / Esc - quit",window->theme.muted_text);
    if(paused && !game_over){
        pg_window_rect(window,(struct pg_rect){sx,cy+110,260,28},0xF9E2AF);
        pg_window_text(window,sx+70,cy+118,"PAUSED",0x1E1E2E);
    }
    if(game_over){
        pg_window_rect(window,(struct pg_rect){sx,cy+110,260,28},0xF38BA8);
        pg_window_text(window,sx+56,cy+118,"GAME OVER",0x1E1E2E);
        pg_window_text(window,sx+10,cy+142,"Press R to restart",window->theme.danger);
    }
}

static void redraw(struct pg_window *window){
    pg_window_begin(window);
    // clear client already done by begin, draw board and side
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
                // lock if cannot move down
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
    struct pg_window window;
    if(!pg_window_center(&window,"Tetris",TETRIS_WIDTH,TETRIS_HEIGHT)) return 1;
    reset_game();
    redraw(&window);
    uint32_t last_gravity=0;
    while(pg_window_is_open(&window)){
        struct pg_event event;
        bool has_event=pg_window_poll_event(&window,&event);
        if(has_event){
            if(event.type==PG_EVENT_REPAINT || event.type==PG_EVENT_MOVE || event.type==PG_EVENT_FOCUS){
                redraw(&window);
            } else {
                handle_input(&window,&event);
            }
            // after handling, check game over close?
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
