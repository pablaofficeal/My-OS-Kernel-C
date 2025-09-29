#ifndef FIELD_8X8_H
#define FIELD_8X8_H

#include "game_common.h"

// Специфичные функции для 8x8
int move_left_8x8(game_2048* game);
int move_right_8x8(game_2048* game);
int move_up_8x8(game_2048* game);
int move_down_8x8(game_2048* game);

#endif