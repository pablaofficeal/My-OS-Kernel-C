#include "game_common.h"
#include "../../lib/string.h"
#include <stdlib.h>
#include "field_4x4.h"
#include "field_8x8.h"
#include "field_16x16.h"

// Объявляем функции которые будем использовать
extern int keyboard_has_data(void);
extern unsigned char keyboard_read_scancode(void);

static unsigned long game_timer = 0;

static unsigned long get_game_timer(void) {
    return ++game_timer;
}

game_2048* game_init(int size) {
    game_2048* game = (game_2048*)malloc(sizeof(game_2048));
    if (!game) return NULL;
    
    game->size = size;
    game->score = 0;
    game->best_score = 0;
    game->game_over = 0;
    game->won = 0;
    game->moved = 0;
    
    // Выделяем память для поля
    game->board = (int**)malloc(size * sizeof(int*));
    for (int i = 0; i < size; i++) {
        game->board[i] = (int*)malloc(size * sizeof(int));
        for (int j = 0; j < size; j++) {
            game->board[i][j] = 0;
        }
    }
    
    // Добавляем 2 начальных плитки
    game_add_random_tile(game);
    game_add_random_tile(game);
    
    return game;
}

void game_cleanup(game_2048* game) {
    if (!game) return;
    
    for (int i = 0; i < game->size; i++) {
        free(game->board[i]);
    }
    free(game->board);
    free(game);
}

int get_color_for_tile(int value) {
    switch (value) {
        case 0: return 0;      // Черный
        case 2: return 14;     // Желтый
        case 4: return 6;      // Коричневый
        case 8: return 12;     // Красный
        case 16: return 4;     // Синий
        case 32: return 2;     // Зеленый
        case 64: return 10;    // Ярко-зеленый
        case 128: return 11;   // Голубой
        case 256: return 5;    // Пурпурный
        case 512: return 13;   // Розовый
        case 1024: return 9;   // Синий
        case 2048: return 15;  // Белый
        default: return 7;     // Серый
    }
}

void draw_tile(int x, int y, int value) {
    char buffer[8];
    
    if (value == 0) {
        printf("    ");
    } else {
        // Устанавливаем цвет
        int color = get_color_for_tile(value);
        
        // Форматируем число
        if (value < 10) {
            printf(" %d  ", value);
        } else if (value < 100) {
            printf(" %d ", value);
        } else if (value < 1000) {
            printf("%d ", value);
        } else {
            printf("%d", value);
        }
    }
}

void game_draw_board(const game_2048* game) {
    int start_x = 5;
    int start_y = 3;
    
    // Рисуем верхнюю границу
    set_cursor(start_x, start_y);
    printf("+");
    for (int j = 0; j < game->size; j++) {
        printf("----");
    }
    printf("+");
    
    // Рисуем поле
    for (int i = 0; i < game->size; i++) {
        set_cursor(start_x, start_y + i + 1);
        printf("|");
        
        for (int j = 0; j < game->size; j++) {
            draw_tile(start_x + 1 + j * 4, start_y + i + 1, game->board[i][j]);
        }
        
        printf("|");
    }
    
    // Рисуем нижнюю границу
    set_cursor(start_x, start_y + game->size + 1);
    printf("+");
    for (int j = 0; j < game->size; j++) {
        printf("----");
    }
    printf("+");
}

void game_draw_ui(const game_2048* game) {
    int ui_x = 30;
    
    // Очищаем область UI
    for (int i = 0; i < 15; i++) {
        set_cursor(ui_x, 3 + i);
        printf("                          ");
    }
    
    // Рисуем UI
    set_cursor(ui_x, 3);
    printf("=== 2048 ===");
    
    set_cursor(ui_x, 5);
    printf("Score: %d", game->score);
    
    set_cursor(ui_x, 6);
    printf("Best: %d", game->best_score);
    
    set_cursor(ui_x, 7);
    printf("Size: %dx%d", game->size, game->size);
    
    set_cursor(ui_x, 9);
    printf("Controls:");
    set_cursor(ui_x, 10);
    printf("W - Up");
    set_cursor(ui_x, 11);
    printf("A - Left");
    set_cursor(ui_x, 12);
    printf("S - Down");
    set_cursor(ui_x, 13);
    printf("D - Right");
    set_cursor(ui_x, 14);
    printf("R - Restart");
    set_cursor(ui_x, 15);
    printf("ESC - Quit");
    
    if (game->won) {
        set_cursor(ui_x, 17);
        printf("*** YOU WIN! ***");
    } else if (game->game_over) {
        set_cursor(ui_x, 17);
        printf("*** GAME OVER ***");
    }
}

void game_draw(const game_2048* game) {
    clear_screen();
    
    // Заголовок
    set_cursor(10, 1);
    printf("=== 2048 GAME ===");
    
    // Рисуем поле и UI
    game_draw_board(game);
    game_draw_ui(game);
}

int count_empty_tiles(const game_2048* game) {
    int count = 0;
    for (int i = 0; i < game->size; i++) {
        for (int j = 0; j < game->size; j++) {
            if (game->board[i][j] == 0) {
                count++;
            }
        }
    }
    return count;
}

int game_add_random_tile(game_2048* game) {
    int empty_count = count_empty_tiles(game);
    if (empty_count == 0) return 0;
    
    // Выбираем случайную пустую клетку
    int target = get_game_timer() % empty_count;
    int value = (get_game_timer() % 10 == 0) ? 4 : 2; // 10% шанс на 4
    
    // Находим и заполняем клетку
    int count = 0;
    for (int i = 0; i < game->size; i++) {
        for (int j = 0; j < game->size; j++) {
            if (game->board[i][j] == 0) {
                if (count == target) {
                    game->board[i][j] = value;
                    return 1;
                }
                count++;
            }
        }
    }
    
    return 0;
}

int game_check_win(const game_2048* game) {
    for (int i = 0; i < game->size; i++) {
        for (int j = 0; j < game->size; j++) {
            if (game->board[i][j] == 2048) {
                return 1;
            }
        }
    }
    return 0;
}

int game_check_game_over(const game_2048* game) {
    // Если есть пустые клетки - игра не окончена
    if (count_empty_tiles(game) > 0) {
        return 0;
    }
    
    // Проверяем возможные ходы
    for (int i = 0; i < game->size; i++) {
        for (int j = 0; j < game->size - 1; j++) {
            if (game->board[i][j] == game->board[i][j + 1]) {
                return 0;
            }
        }
    }
    
    for (int j = 0; j < game->size; j++) {
        for (int i = 0; i < game->size - 1; i++) {
            if (game->board[i][j] == game->board[i + 1][j]) {
                return 0;
            }
        }
    }
    
    return 1;
}

void game_draw_game_over(const game_2048* game) {
    clear_screen();
    
    set_cursor(30, 8);
    if (game->won) {
        printf("*** YOU WIN! ***");
    } else {
        printf("*** GAME OVER ***");
    }
    
    set_cursor(30, 10);
    printf("Final Score: %d", game->score);
    set_cursor(30, 11);
    printf("Best Score: %d", game->best_score);
    set_cursor(30, 13);
    printf("Press any key...");
    
    // Ждем нажатия любой клавиши
    while (!keyboard_has_data()) {
        for (volatile int i = 0; i < 10000; i++);
    }
    
    // Считываем клавишу чтобы очистить буфер
    keyboard_read_scancode();
}

// В КОНЕЦ src/game/2048/game_common.c добавить:

// Обертки для вызова правильных функций движения
int move_left(game_2048* game) {
    switch (game->size) {
        case 4: return move_left_4x4(game);
        case 8: return move_left_8x8(game);
        case 16: return move_left_16x16(game);
        default: return 0;
    }
}

int move_right(game_2048* game) {
    switch (game->size) {
        case 4: return move_right_4x4(game);
        case 8: return move_right_8x8(game);
        case 16: return move_right_16x16(game);
        default: return 0;
    }
}

int move_up(game_2048* game) {
    switch (game->size) {
        case 4: return move_up_4x4(game);
        case 8: return move_up_8x8(game);
        case 16: return move_up_16x16(game);
        default: return 0;
    }
}

int move_down(game_2048* game) {
    switch (game->size) {
        case 4: return move_down_4x4(game);
        case 8: return move_down_8x8(game);
        case 16: return move_down_16x16(game);
        default: return 0;
    }
}