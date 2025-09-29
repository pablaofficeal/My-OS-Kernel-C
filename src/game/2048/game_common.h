#ifndef GAME_COMMON_H
#define GAME_COMMON_H

#include "2048.h"
#include <stdlib.h>  // Добавьте для malloc/free

// Общие функции для всех размеров поля
game_2048* game_init(int size);
void game_cleanup(game_2048* game);
void game_draw(const game_2048* game);
void game_handle_input(game_2048* game);
int game_add_random_tile(game_2048* game);
int game_check_game_over(const game_2048* game);
int game_check_win(const game_2048* game);
void game_draw_board(const game_2048* game);
void game_draw_ui(const game_2048* game);
void game_draw_game_over(const game_2048* game);

// Функции движения
int move_left(game_2048* game);
int move_right(game_2048* game);
int move_up(game_2048* game);
int move_down(game_2048* game);

// Вспомогательные функции
int get_color_for_tile(int value);
void draw_tile(int x, int y, int value);
int count_empty_tiles(const game_2048* game);

#endif