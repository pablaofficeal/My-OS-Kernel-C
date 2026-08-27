#include "snake.h"

#include "../../drivers/keyboard.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../kernel/syscall.h"
#include "../../kernel/system_info.h"
#include "../syscall.h"
#include "../terminal/terminal.h"
#include <stdbool.h>
#include <stdint.h>

#define SNAKE_BOARD_WIDTH  30
#define SNAKE_BOARD_HEIGHT 16
#define SNAKE_MAX_LENGTH   (SNAKE_BOARD_WIDTH*SNAKE_BOARD_HEIGHT)
#define SNAKE_SPEED_HZ     6

struct snake_point {
    int16_t x;
    int16_t y;
};

struct snake_game {
    struct snake_point body[SNAKE_MAX_LENGTH];
    struct snake_point food;
    int16_t direction_x;
    int16_t direction_y;
    uint16_t length;
    uint32_t score;
    uint32_t random_state;
    bool running;
    bool game_over;
};

static uint64_t read_tsc(void){
    uint32_t low,high;
    __asm__ volatile("rdtsc":"=a"(low),"=d"(high));
    return ((uint64_t)high<<32)|low;
}

static uint32_t snake_random(struct snake_game *game){
    uint32_t value=game->random_state;
    value^=value<<13;
    value^=value>>17;
    value^=value<<5;
    game->random_state=value ? value : 0xA341316C;
    return game->random_state;
}

static bool point_is_on_snake(const struct snake_game *game, int16_t x, int16_t y){
    for(uint16_t index=0;index<game->length;index++){
        if(game->body[index].x==x && game->body[index].y==y) return true;
    }
    return false;
}

static void place_food(struct snake_game *game){
    for(uint16_t attempt=0;attempt<SNAKE_MAX_LENGTH;attempt++){
        int16_t x=(int16_t)(snake_random(game)%SNAKE_BOARD_WIDTH);
        int16_t y=(int16_t)(snake_random(game)%SNAKE_BOARD_HEIGHT);
        if(!point_is_on_snake(game,x,y)){
            game->food.x=x;
            game->food.y=y;
            return;
        }
    }
    game->running=false;
}

static void initialize_game(struct snake_game *game){
    game->length=4;
    game->score=0;
    game->direction_x=1;
    game->direction_y=0;
    game->running=true;
    game->game_over=false;
    game->random_state=(uint32_t)read_tsc();

    int16_t start_x=SNAKE_BOARD_WIDTH/2;
    int16_t start_y=SNAKE_BOARD_HEIGHT/2;
    for(uint16_t index=0;index<game->length;index++){
        game->body[index].x=start_x-(int16_t)index;
        game->body[index].y=start_y;
    }
    place_food(game);
}

static void draw_game(const struct snake_game *game){
    char line[SNAKE_BOARD_WIDTH+3];
    terminal_clear();
    terminal_printf("Snake  Score: %u  WASD: move  Q/Esc: exit\n",game->score);

    line[0]='+';
    for(uint16_t x=0;x<SNAKE_BOARD_WIDTH;x++) line[x+1]='-';
    line[SNAKE_BOARD_WIDTH+1]='+';
    line[SNAKE_BOARD_WIDTH+2]='\0';
    terminal_write(line);
    terminal_putc('\n');

    for(int16_t y=0;y<SNAKE_BOARD_HEIGHT;y++){
        line[0]='|';
        for(int16_t x=0;x<SNAKE_BOARD_WIDTH;x++){
            char cell=' ';
            if(game->food.x==x && game->food.y==y) cell='@';
            for(uint16_t index=0;index<game->length;index++){
                if(game->body[index].x==x && game->body[index].y==y){
                    cell=index==0 ? 'O' : 'o';
                    break;
                }
            }
            line[x+1]=cell;
        }
        line[SNAKE_BOARD_WIDTH+1]='|';
        line[SNAKE_BOARD_WIDTH+2]='\0';
        terminal_write(line);
        terminal_putc('\n');
    }

    terminal_write("+");
    for(uint16_t x=0;x<SNAKE_BOARD_WIDTH;x++) terminal_putc('-');
    terminal_write("+\n");
}

static void handle_key(struct snake_game *game, char key){
    if(key=='q' || key=='Q' || key==27){
        game->running=false;
        return;
    }
    if((key=='w' || key=='W') && game->direction_y!=1){
        game->direction_x=0;
        game->direction_y=-1;
    } else if((key=='s' || key=='S') && game->direction_y!=-1){
        game->direction_x=0;
        game->direction_y=1;
    } else if((key=='a' || key=='A') && game->direction_x!=1){
        game->direction_x=-1;
        game->direction_y=0;
    } else if((key=='d' || key=='D') && game->direction_x!=-1){
        game->direction_x=1;
        game->direction_y=0;
    }
}

static void advance_game(struct snake_game *game){
    struct snake_point next={
        .x=game->body[0].x+game->direction_x,
        .y=game->body[0].y+game->direction_y
    };
    bool ate_food=next.x==game->food.x && next.y==game->food.y;

    if(next.x<0 || next.x>=SNAKE_BOARD_WIDTH
       || next.y<0 || next.y>=SNAKE_BOARD_HEIGHT){
        game->game_over=true;
        game->running=false;
        return;
    }

    uint16_t collision_length=ate_food ? game->length : game->length-1;
    for(uint16_t index=0;index<collision_length;index++){
        if(game->body[index].x==next.x && game->body[index].y==next.y){
            game->game_over=true;
            game->running=false;
            return;
        }
    }

    if(ate_food && game->length<SNAKE_MAX_LENGTH) game->length++;
    for(uint16_t index=game->length-1;index>0;index--){
        game->body[index]=game->body[index-1];
    }
    game->body[0]=next;

    if(ate_food){
        game->score++;
        if(game->length==SNAKE_MAX_LENGTH) game->running=false;
        else place_food(game);
    }
}

void snake_run(void){
    struct snake_game game;
    initialize_game(&game);

    uint64_t tick_interval=system_info_tsc_frequency_hz()/SNAKE_SPEED_HZ;
    uint64_t next_tick=read_tsc()+tick_interval;
    draw_game(&game);

    while(game.running){
        ps2_mouse_poll();
        char key;
        while(keyboard_try_getc(&key)) handle_key(&game,key);
        if(!game.running) break;

        uint64_t now=read_tsc();
        if(now>=next_tick){
            advance_game(&game);
            draw_game(&game);
            next_tick=now+tick_interval;
        }
        (void)userspace_syscall(SYS_SCHED_YIELD,0,0,0);
        __asm__ volatile("pause");
    }

    terminal_clear();
    if(game.game_over) terminal_printf("Game over. Score: %u\n",game.score);
    else terminal_printf("Snake closed. Score: %u\n",game.score);
}
