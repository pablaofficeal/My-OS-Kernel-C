#include "field_8x8.h"

int move_left_8x8(game_2048* game) {
    int moved = 0;
    
    for (int i = 0; i < 8; i++) {
        // Сдвигаем влево
        int write_pos = 0;
        for (int j = 0; j < 8; j++) {
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
        for (int j = 0; j < 7; j++) {
            if (game->board[i][j] != 0 && game->board[i][j] == game->board[i][j + 1]) {
                game->board[i][j] *= 2;
                game->board[i][j + 1] = 0;
                game->score += game->board[i][j];
                moved = 1;
                
                // Сдвигаем оставшиеся
                for (int k = j + 1; k < 7; k++) {
                    game->board[i][k] = game->board[i][k + 1];
                }
                game->board[i][7] = 0;
            }
        }
    }
    
    return moved;
}

int move_right_8x8(game_2048* game) {
    int moved = 0;
    
    for (int i = 0; i < 8; i++) {
        // Сдвигаем вправо
        int write_pos = 7;
        for (int j = 7; j >= 0; j--) {
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
        for (int j = 7; j > 0; j--) {
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

int move_up_8x8(game_2048* game) {
    int moved = 0;
    
    for (int j = 0; j < 8; j++) {
        // Сдвигаем вверх
        int write_pos = 0;
        for (int i = 0; i < 8; i++) {
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
        for (int i = 0; i < 7; i++) {
            if (game->board[i][j] != 0 && game->board[i][j] == game->board[i + 1][j]) {
                game->board[i][j] *= 2;
                game->board[i + 1][j] = 0;
                game->score += game->board[i][j];
                moved = 1;
                
                // Сдвигаем оставшиеся
                for (int k = i + 1; k < 7; k++) {
                    game->board[k][j] = game->board[k + 1][j];
                }
                game->board[7][j] = 0;
            }
        }
    }
    
    return moved;
}

int move_down_8x8(game_2048* game) {
    int moved = 0;
    
    for (int j = 0; j < 8; j++) {
        // Сдвигаем вниз
        int write_pos = 7;
        for (int i = 7; i >= 0; i--) {
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
        for (int i = 7; i > 0; i--) {
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