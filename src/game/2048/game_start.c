#include "2048.h"
#include "field_4x4.h"
#include "field_8x8.h"
#include "field_16x16.h"
#include "../../lib/string.h"

// Объявляем функции которые будем использовать
extern int keyboard_has_data(void);
extern unsigned char keyboard_read_scancode(void);

void show_size_selection(void) {
    clear_screen();
    
    set_cursor(30, 5);
    printf("=== 2048 ===");
    set_cursor(30, 7);
    printf("Select board size:");
    set_cursor(30, 9);
    printf("1 - 4x4 (Classic)");
    set_cursor(30, 10);
    printf("2 - 8x8 (Medium)");
    set_cursor(30, 11);
    printf("3 - 16x16 (Expert)");
    set_cursor(30, 13);
    printf("Press 1, 2 or 3...");
    
    // Ждем выбора размера
    while (1) {
        if (keyboard_has_data()) {
            unsigned char scancode = keyboard_read_scancode();
            
            // Обрабатываем только нажатия (без отпускания)
            if (scancode & 0x80) continue;
            
            switch (scancode) {
                case 0x02: // 1
                    start_4x4_game();
                    return;
                case 0x03: // 2
                    start_8x8_game();
                    return;
                case 0x04: // 3
                    start_16x16_game();
                    return;
                case 0x01: // ESC
                    return;
            }
        }
        
        // Небольшая задержка
        for (volatile int i = 0; i < 10000; i++);
    }
}

void start_4x4_game(void) {
    printf("\nStarting 4x4 game...\n");
    game_2048_loop(4);
}

void start_8x8_game(void) {
    printf("\nStarting 8x8 game...\n");
    game_2048_loop(8);
}

void start_16x16_game(void) {
    printf("\nStarting 16x16 game...\n");
    game_2048_loop(16);
}

void game_2048_handle_input(game_2048* game) {
    if (!keyboard_has_data()) return;
    
    unsigned char scancode = keyboard_read_scancode();
    
    // Обрабатываем только нажатия (без отпускания)
    if (scancode & 0x80) return;
    
    int moved = 0;
    
    switch (scancode) {
        case 0x11: // W - вверх
            moved = move_up(game);
            break;
        case 0x1F: // S - вниз
            moved = move_down(game);
            break;
        case 0x1E: // A - влево
            moved = move_left(game);
            break;
        case 0x20: // D - вправо
            moved = move_right(game);
            break;
        case 0x13: // R - рестарт
            // Перезапуск игры
            game_cleanup(game);
            show_size_selection();
            return;
        case 0x01: // ESC - выход
            game->game_over = 1;
            break;
    }
    
    if (moved) {
        game_add_random_tile(game);
        game->moved = 1;
        
        // Проверяем победу
        if (!game->won && game_check_win(game)) {
            game->won = 1;
        }
        
        // Проверяем конец игры
        if (game_check_game_over(game)) {
            game->game_over = 1;
        }
    }
}

void game_2048_loop(int size) {
    game_2048* game = game_init(size);
    if (!game) {
        printf("Failed to initialize game!\n");
        return;
    }
    
    // Основной игровой цикл
    while (!game->game_over) {
        game_draw(game);
        game_2048_handle_input(game);
        
        // Небольшая задержка для стабильности
        for (volatile int i = 0; i < 1000; i++);
    }
    
    // Обновляем лучший счет
    if (game->score > game->best_score) {
        game->best_score = game->score;
    }
    
    game_draw_game_over(game);
    game_cleanup(game);
    
    // Возвращаемся к выбору размера
    show_size_selection();
}