#ifndef GAME_2048_H
#define GAME_2048_H

#include "../../drivers/screen.h"
#include "../../drivers/keyboard.h"

#define MAX_BOARD_SIZE 16

// Структура игры
typedef struct {
    int** board;
    int size;
    int score;
    int best_score;
    int game_over;
    int won;
    int moved;
} game_2048;

// Функции выбора размера поля
void show_size_selection(void);
void start_4x4_game(void);
void start_8x8_game(void);
void start_16x16_game(void);

// Основной игровой цикл
void game_2048_loop(int size);

#endif