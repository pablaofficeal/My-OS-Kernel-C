#include "../../../libc/include/purec.h"

#define BOARD_WIDTH 30
#define BOARD_HEIGHT 18
#define CELL_SIZE 18
#define MAX_LENGTH (BOARD_WIDTH*BOARD_HEIGHT)
#define COLOR_BACKGROUND 0x181825
#define COLOR_PANEL 0x313244
#define COLOR_SNAKE 0xA6E3A1
#define COLOR_HEAD 0x89B4FA
#define COLOR_FOOD 0xF38BA8
#define COLOR_TEXT 0xCDD6F4

struct point { int16_t x; int16_t y; };

static struct point body[MAX_LENGTH];
static struct point food;
static uint16_t length;
static int16_t direction_x;
static int16_t direction_y;
static uint32_t random_state=0x51A7E123;
static uint32_t score;

static uint32_t random_value(void){
    random_state^=random_state<<13;
    random_state^=random_state>>17;
    random_state^=random_state<<5;
    return random_state;
}

static bool occupied(int16_t x, int16_t y){
    for(uint16_t index=0;index<length;index++){
        if(body[index].x==x && body[index].y==y) return true;
    }
    return false;
}

static void place_food(void){
    do {
        food.x=(int16_t)(random_value()%BOARD_WIDTH);
        food.y=(int16_t)(random_value()%BOARD_HEIGHT);
    } while(occupied(food.x,food.y));
}

static void draw(uint32_t origin_x, uint32_t origin_y){
    pc_display_clear(COLOR_BACKGROUND);
    pc_draw_text(origin_x,origin_y-28,"Snake - WASD to move, Q to exit",
                 COLOR_TEXT,COLOR_BACKGROUND);
    pc_draw_rect(origin_x-4,origin_y-4,BOARD_WIDTH*CELL_SIZE+8,
                 BOARD_HEIGHT*CELL_SIZE+8,COLOR_PANEL);
    pc_draw_rect(origin_x+food.x*CELL_SIZE,origin_y+food.y*CELL_SIZE,
                 CELL_SIZE-2,CELL_SIZE-2,COLOR_FOOD);
    for(uint16_t index=0;index<length;index++){
        pc_draw_rect(origin_x+body[index].x*CELL_SIZE,
                     origin_y+body[index].y*CELL_SIZE,
                     CELL_SIZE-2,CELL_SIZE-2,index ? COLOR_SNAKE : COLOR_HEAD);
    }
}

static bool advance(void){
    struct point next={
        (int16_t)(body[0].x+direction_x),
        (int16_t)(body[0].y+direction_y)
    };
    if(next.x<0 || next.x>=BOARD_WIDTH || next.y<0 || next.y>=BOARD_HEIGHT)
        return false;
    bool ate=next.x==food.x && next.y==food.y;
    uint16_t collision_length=ate ? length : length-1;
    for(uint16_t index=0;index<collision_length;index++){
        if(body[index].x==next.x && body[index].y==next.y) return false;
    }
    if(ate && length<MAX_LENGTH){ length++; score++; }
    for(uint16_t index=length-1;index>0;index--) body[index]=body[index-1];
    body[0]=next;
    if(ate) place_food();
    return true;
}

static void handle_key(int32_t key, bool *running){
    if(key=='q' || key=='Q' || key==27){ *running=false; return; }
    if((key=='w' || key=='W') && direction_y!=1){
        direction_x=0; direction_y=-1;
    } else if((key=='s' || key=='S') && direction_y!=-1){
        direction_x=0; direction_y=1;
    } else if((key=='a' || key=='A') && direction_x!=1){
        direction_x=-1; direction_y=0;
    } else if((key=='d' || key=='D') && direction_x!=-1){
        direction_x=1; direction_y=0;
    }
}

static int snake_main(void){
    struct pc_display_info display;
    if(!pc_display_get_info(&display)) return 1;
    length=4;
    direction_x=1;
    direction_y=0;
    score=0;
    for(uint16_t index=0;index<length;index++){
        body[index].x=BOARD_WIDTH/2-(int16_t)index;
        body[index].y=BOARD_HEIGHT/2;
    }
    place_food();
    uint32_t board_width=BOARD_WIDTH*CELL_SIZE;
    uint32_t board_height=BOARD_HEIGHT*CELL_SIZE;
    uint32_t origin_x=display.width>board_width
        ? (display.width-board_width)/2 : 0;
    uint32_t origin_y=display.height>board_height+40
        ? (display.height-board_height)/2+20 : 40;
    bool running=true;
    while(running){
        int32_t key;
        while((key=pc_try_getchar())>=0) handle_key(key,&running);
        if(!running || !advance()) break;
        draw(origin_x,origin_y);
        pc_sleep(140);
    }
    pc_display_clear(COLOR_BACKGROUND);
    pc_draw_text(40,60,"Snake finished",COLOR_TEXT,COLOR_BACKGROUND);
    pc_sleep(900);
    return 0;
}

void _start(void){
    pc_exit(snake_main());
}
