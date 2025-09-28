#include "field_4x4.h"

int move_left_4x4(game_2048* game) {
    int moved = 0;
    
    for (int i = 0; i < 4; i++) {
        // Сдвигаем влево
        int write_pos = 0;
        for (int j = 0; j < 4; j++) {
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
        for (int j = 0; j < 3; j++) {
            if (game->board[i][j] != 0 && game->board[i][j] == game->board[i][j + 1]) {
                game->board[i][j] *= 2;
                game->board[i][j + 1] = 0;
                game->score += game->board[i][j];
                moved = 1;
                
                // Сдвигаем оставшиеся
                for (int k = j + 1; k < 3; k++) {
                    game->board[i][k] = game->board[i][k + 1];
                }
                game->board[i][3] = 0;
            }
        }
    }
    
    return moved;
}

int move_right_4x4(game_2048* game) {
    int moved = 0;
    
    for (int i = 0; i < 4; i++) {
        // Сдвигаем вправо
        int write_pos = 3;
        for (int j = 3; j >= 0; j--) {
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
        for (int j = 3; j > 0; j--) {
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

int move_up_4x4(game_2048* game) {
    int moved = 0;
    
    for (int j = 0; j < 4; j++) {
        // Сдвигаем вверх
        int write_pos = 0;
        for (int i = 0; i < 4; i++) {
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
        for (int i = 0; i < 3; i++) {
            if (game->board[i][j] != 0 && game->board[i][j] == game->board[i + 1][j]) {
                game->board[i][j] *= 2;
                game->board[i + 1][j] = 0;
                game->score += game->board[i][j];
                moved = 1;
                
                // Сдвигаем оставшиеся
                for (int k = i + 1; k < 3; k++) {
                    game->board[k][j] = game->board[k + 1][j];
                }
                game->board[3][j] = 0;
            }
        }
    }
    
    return moved;
}

int move_down_4x4(game_2048* game) {
    int moved = 0;
    
    for (int j = 0; j < 4; j++) {
        // Сдвигаем вниз
        int write_pos = 3;
        for (int i = 3; i >= 0; i--) {
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
        for (int i = 3; i > 0; i--) {
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