// src/game/snake.h
#ifndef SNAKE_H
#define SNAKE_H

#include "./drivers/screen.h"
#include "./drivers/keyboard/keyboard.h"

#define SNAKE_WIDTH 120
#define SNAKE_HEIGHT 40
#define SNAKE_MAX_LENGTH 100

// Направления
typedef enum {
    SNAKE_UP,
    SNAKE_DOWN, 
    SNAKE_LEFT,
    SNAKE_RIGHT
} snake_direction_t;

// Структура змейки
typedef struct {
    int x[SNAKE_MAX_LENGTH];
    int y[SNAKE_MAX_LENGTH];
    int length;
    snake_direction_t direction;
    int score;
} snake_t;

// Структура еды
typedef struct {
    int x;
    int y;
    int active;
} food_t;

// Функции игры
void snake_init(void);
void snake_game_loop(void);
void snake_draw_board(void);
void snake_move(void);
void snake_handle_input(void);
void snake_spawn_food(void);
int snake_check_collision(void);
void snake_draw_snake(void);
void snake_draw_food(void);
void snake_show_game_over(void);

#endif