#include "field_16x16.h"

int move_left_16x16(game_2048* game) {
    int moved = 0;
    
    for (int i = 0; i < 16; i++) {
        // Сдвигаем влево
        int write_pos = 0;
        for (int j = 0; j < 16; j++) {
            if (game->board[i][j] != 0) {
                if (write_pos != j) {
                    game->board[i][write_pos] = game->board[i][j];
                    game->board[i][j] = 0;
                    moved = 1;
                }
                write_pos++;
            }
        }
        
        // Объединяем
        for (int j = 0; j < 15; j++) {
            if (game->board[i][j] != 0 && game->board[i][j] == game->board[i][j + 1]) {
                game->board[i][j] *= 2;
                game->board[i][j + 1] = 0;
                game->score += game->board[i][j];
                moved = 1;
                
                // Сдвигаем оставшиеся
                for (int k = j + 1; k < 15; k++) {
                    game->board[i][k] = game->board[i][k + 1];
                }
                game->board[i][15] = 0;
            }
        }
    }
    
    return moved;
}

int move_right_16x16(game_2048* game) {
    int moved = 0;
    
    for (int i = 0; i < 16; i++) {
        // Сдвигаем вправо
        int write_pos = 15;
        for (int j = 15; j >= 0; j--) {
            if (game->board[i][j] != 0) {
                if (write_pos != j) {
                    game->board[i][write_pos] = game->board[i][j];
                    game->board[i][j] = 0;
                    moved = 1;
                }
                write_pos--;
            }
        }
        
        // Объединяем
        for (int j = 15; j > 0; j--) {
            if (game->board[i][j] != 0 && game->board[i][j] == game->board[i][j - 1]) {
                game->board[i][j] *= 2;
                game->board[i][j - 1] = 0;
                game->score += game->board[i][j];
                moved = 1;
                
                // Сдвигаем оставшиеся
                for (int k = j - 1; k > 0; k--) {
                    game->board[i][k] = game->board[i][k - 1];
                }
                game->board[i][0] = 0;
            }
        }
    }
    
    return moved;
}

int move_up_16x16(game_2048* game) {
    int moved = 0;
    
    for (int j = 0; j < 16; j++) {
        // Сдвигаем вверх
        int write_pos = 0;
        for (int i = 0; i < 16; i++) {
            if (game->board[i][j] != 0) {
                if (write_pos != i) {
                    game->board[write_pos][j] = game->board[i][j];
                    game->board[i][j] = 0;
                    moved = 1;
                }
                write_pos++;
            }
        }
        
        // Объединяем
        for (int i = 0; i < 15; i++) {
            if (game->board[i][j] != 0 && game->board[i][j] == game->board[i + 1][j]) {
                game->board[i][j] *= 2;
                game->board[i + 1][j] = 0;
                game->score += game->board[i][j];
                moved = 1;
                
                // Сдвигаем оставшиеся
                for (int k = i + 1; k < 15; k++) {
                    game->board[k][j] = game->board[k + 1][j];
                }
                game->board[15][j] = 0;
            }
        }
    }
    
    return moved;
}

int move_down_16x16(game_2048* game) {
    int moved = 0;
    
    for (int j = 0; j < 16; j++) {
        // Сдвигаем вниз
        int write_pos = 15;
        for (int i = 15; i >= 0; i--) {
            if (game->board[i][j] != 0) {
                if (write_pos != i) {
                    game->board[write_pos][j] = game->board[i][j];
                    game->board[i][j] = 0;
                    moved = 1;
                }
                write_pos--;
            }
        }
        
        // Объединяем
        for (int i = 15; i > 0; i--) {
            if (game->board[i][j] != 0 && game->board[i][j] == game->board[i - 1][j]) {
                game->board[i][j] *= 2;
                game->board[i - 1][j] = 0;
                game->score += game->board[i][j];
                moved = 1;
                
                // Сдвигаем оставшиеся
                for (int k = i - 1; k > 0; k--) {
                    game->board[k][j] = game->board[k - 1][j];
                }
                game->board[0][j] = 0;
            }
        }
    }
    
    return moved;
}