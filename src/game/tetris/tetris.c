#include "tetris.h"
#include "../../lib/string.h"


static tetris_game game;
static unsigned long game_timer = 0;

// Объявляем функции которые будем использовать
extern int keyboard_has_data(void);
extern unsigned char keyboard_read_scancode(void);

// Формы тетрамино
static const int tetris_shapes[TETRIS_COUNT][4][4][4] = {
    // I
    {
        {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0}},
        {{0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0}}
    },
    // J
    {
        {{1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0}}
    },
    // L
    {
        {{0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}}
    },
    // O
    {
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}}
    },
    // S
    {
        {{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0}},
        {{1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}}
    },
    // T
    {
        {{0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}}
    },
    // Z
    {
        {{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0}}
    }
};

// Цвета для каждого тетрамино
static const char* tetris_colors[TETRIS_COUNT] = {
    TETRIS_COLOR_CYAN,    // I
    TETRIS_COLOR_BLUE,    // J
    TETRIS_COLOR_ORANGE,  // L
    TETRIS_COLOR_YELLOW,  // O
    TETRIS_COLOR_GREEN,   // S
    TETRIS_COLOR_PURPLE,  // T
    TETRIS_COLOR_RED      // Z
};

static unsigned long get_game_timer(void) {
    return ++game_timer;
}

void tetris_init(void) {
    // Очищаем игровое поле
    memset(game.field, 0, sizeof(game.field));
    
    // Устанавливаем границы
    for (int y = 0; y < FIELD_HEIGHT; y++) {
        game.field[y][0] = 9; // Левая граница
        game.field[y][FIELD_WIDTH - 1] = 9; // Правая граница
    }
    for (int x = 0; x < FIELD_WIDTH; x++) {
        game.field[FIELD_HEIGHT - 1][x] = 9; // Нижняя граница
    }
    
    // Инициализируем игровые переменные
    game.score = 0;
    game.level = 1;
    game.lines_cleared = 0;
    game.game_over = 0;
    game.paused = 0;
    game.last_drop_time = 0;
    
    // Создаем первую фигуру
    tetris_spawn_piece();
    
    printf("Tetris initialized! Use WASD to play, P to pause\n");
}

void tetris_spawn_piece(void) {
    // Выбираем случайный тип фигуры
    tetris_piece_t type = get_game_timer() % TETRIS_COUNT;
    
    // Инициализируем текущую фигуру
    game.current_piece.type = type;
    game.current_piece.x = TETRIS_WIDTH / 2 - 2;
    game.current_piece.y = 0;
    game.current_piece.rotation = 0;
    game.current_piece.color = tetris_colors[type];
    
    // Копируем форму
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            game.current_piece.shape[i][j] = tetris_shapes[type][0][i][j];
        }
    }
    
    // Проверяем game over
    if (tetris_check_collision()) {
        game.game_over = 1;
    }
}

void tetris_draw_piece(const tetris_piece* piece, int draw) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (piece->shape[i][j]) {
                int screen_x = piece->x + j + 1; // +1 для границы
                int screen_y = piece->y + i;
                
                if (screen_y >= 0 && screen_y < TETRIS_HEIGHT) {
                    set_cursor(screen_x * 2, screen_y + 2); // *2 для ширины блоков
                    if (draw) {
                        putchar('[');
                        putchar(']');
                    } else {
                        putchar(' ');
                        putchar(' ');
                    }
                }
            }
        }
    }
}

void tetris_draw_field(void) {
    for (int y = 0; y < TETRIS_HEIGHT; y++) {
        for (int x = 0; x < FIELD_WIDTH; x++) {
            set_cursor(x * 2, y + 2);
            
            if (game.field[y][x] == 9) { // Граница
                putchar('#');
                putchar('#');
            } else if (game.field[y][x] > 0) { // Заблокированная фигура
                putchar('[');
                putchar(']');
            } else { // Пустое место
                putchar(' ');
                putchar(' ');
            }
        }
    }
}

void tetris_draw_ui(void) {
    // Очищаем область UI
    for (int y = 0; y < 5; y++) {
        set_cursor(70, y + 2);
        printf("                    ");
    }
    
    // Рисуем статистику
    set_cursor(70, 2);
    printf("=== TETRIS ===");
    
    set_cursor(70, 4);
    printf("Score: %d", game.score);
    
    set_cursor(70, 5);
    printf("Level: %d", game.level + 1);
    
    set_cursor(70, 6);
    printf("Lines: %d", game.lines_cleared);
    
    set_cursor(70, 8);
    printf("Next:");
    
    set_cursor(70, 8);
    printf("Controls:");
    set_cursor(70, 9);
    printf("←→ - Move");
    set_cursor(70, 10);
    printf("↓ - Soft drop");
    set_cursor(70, 11);
    printf("↑ - Rotate");
    set_cursor(70, 12);
    printf("Space - Hard drop");
    set_cursor(70, 13);
    printf("Q - Quit");
    
    if (game.paused) {
        set_cursor(25, 17);
        printf("*** PAUSED ***");
    }
}

void tetris_draw_game(void) {
    clear_screen();
    
    // Рисуем заголовок
    set_cursor(5, 0);
    printf("=== TETRIS ===");
    
    // Рисуем игровое поле
    tetris_draw_field();
    tetris_draw_piece(&game.current_piece, 1);
    tetris_draw_ui();
}

int tetris_check_collision(void) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (game.current_piece.shape[i][j]) {
                int field_x = game.current_piece.x + j + 1;
                int field_y = game.current_piece.y + i;
                
                // Проверяем выход за границы и столкновение с заблокированными фигурами
                if (field_x < 1 || field_x >= FIELD_WIDTH - 1 || 
                    field_y >= FIELD_HEIGHT - 1 || 
                    game.field[field_y][field_x] > 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

void tetris_move_piece(int dx, int dy) {
    if (game.paused || game.game_over) return;
    
    // Сохраняем старую позицию
    int old_x = game.current_piece.x;
    int old_y = game.current_piece.y;
    
    // Пробуем переместить
    game.current_piece.x += dx;
    game.current_piece.y += dy;
    
    // Если столкновение - возвращаем обратно
    if (tetris_check_collision()) {
        game.current_piece.x = old_x;
        game.current_piece.y = old_y;
        
        // Если движение вниз вызвало столкновение - фиксируем фигуру
        if (dy > 0) {
            tetris_lock_piece();
        }
    } else {
        // Перерисовываем
        tetris_draw_piece(&game.current_piece, 0);
        game.current_piece.x = old_x;
        game.current_piece.y = old_y;
        tetris_draw_field();
        
        game.current_piece.x += dx;
        game.current_piece.y += dy;
        tetris_draw_piece(&game.current_piece, 1);
    }
}

void tetris_rotate_piece(void) {
    if (game.paused || game.game_over) return;
    
    // Сохраняем старый поворот
    int old_rotation = game.current_piece.rotation;
    int old_shape[4][4];
    
    // Копируем старую форму
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            old_shape[i][j] = game.current_piece.shape[i][j];
        }
    }
    
    // Поворачиваем
    game.current_piece.rotation = (game.current_piece.rotation + 1) % 4;
    tetris_piece_t type = game.current_piece.type;
    
    // Копируем новую форму
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            game.current_piece.shape[i][j] = tetris_shapes[type][game.current_piece.rotation][i][j];
        }
    }
    
    // Если столкновение - возвращаем обратно
    if (tetris_check_collision()) {
        game.current_piece.rotation = old_rotation;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                game.current_piece.shape[i][j] = old_shape[i][j];
            }
        }
    } else {
        // Перерисовываем
        tetris_draw_piece(&game.current_piece, 0);
        game.current_piece.rotation = old_rotation;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                game.current_piece.shape[i][j] = old_shape[i][j];
            }
        }
        tetris_draw_field();
        
        game.current_piece.rotation = (old_rotation + 1) % 4;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                game.current_piece.shape[i][j] = tetris_shapes[type][game.current_piece.rotation][i][j];
            }
        }
        tetris_draw_piece(&game.current_piece, 1);
    }
}

void tetris_drop_piece(void) {
    if (game.paused || game.game_over) return;
    
    // Падаем пока не столкнемся
    while (!tetris_check_collision()) {
        game.current_piece.y++;
    }
    game.current_piece.y--; // Возвращаем на последнюю валидную позицию
    tetris_lock_piece();
}

void tetris_lock_piece(void) {
    // Фиксируем фигуру на поле
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (game.current_piece.shape[i][j]) {
                int field_x = game.current_piece.x + j + 1;
                int field_y = game.current_piece.y + i;
                
                if (field_y >= 0) { // Не фиксируем за верхней границей
                    game.field[field_y][field_x] = game.current_piece.type + 1;
                }
            }
        }
    }
    
    // Очищаем линии
    int lines_cleared = tetris_clear_lines();
    
    // Обновляем счет
    if (lines_cleared > 0) {
        game.lines_cleared += lines_cleared;
        game.score += lines_cleared * 100 * game.level;
        game.level = game.lines_cleared / 10 + 1;
    }
    
    // Создаем новую фигуру
    tetris_spawn_piece();
}

int tetris_clear_lines(void) {
    int lines_cleared = 0;
    
    for (int y = TETRIS_HEIGHT - 2; y >= 0; y--) {
        int line_full = 1;
        
        // Проверяем заполнена ли линия
        for (int x = 1; x < FIELD_WIDTH - 1; x++) {
            if (game.field[y][x] == 0) {
                line_full = 0;
                break;
            }
        }
        
        if (line_full) {
            lines_cleared++;
            
            // Сдвигаем все линии выше вниз
            for (int y2 = y; y2 > 0; y2--) {
                for (int x = 1; x < FIELD_WIDTH - 1; x++) {
                    game.field[y2][x] = game.field[y2 - 1][x];
                }
            }
            
            // Очищаем верхнюю линию
            for (int x = 1; x < FIELD_WIDTH - 1; x++) {
                game.field[0][x] = 0;
            }
            
            // Проверяем эту же позицию снова (т.к. сдвинули линии)
            y++;
        }
    }
    
    return lines_cleared;
}

unsigned long tetris_get_drop_speed(void) {
    // Скорость падения зависит от уровня
    const unsigned long speeds[] = {800000, 700000, 600000, 500000, 400000, 
                                   300000, 250000, 200000, 150000, 100000};
    int level_index = (game.level - 1) % 10;
    return speeds[level_index];
}

void tetris_handle_input(void) {
    if (!keyboard_has_data()) return;
    
    unsigned char scancode = keyboard_read_scancode();
    
    // Обрабатываем только нажатия (без отпускания)
    if (scancode & 0x80) return;
    
    switch (scancode) {
        case 0x1E: // A - влево
            tetris_move_piece(-1, 0);
            break;
        case 0x20: // D - вправо
            tetris_move_piece(1, 0);
            break;
        case 0x1F: // S - вниз
            tetris_move_piece(0, 1);
            break;
        case 0x11: // W - поворот
            tetris_rotate_piece();
            break;
        case 0x39: // Space - сброс
            tetris_drop_piece();
            break;
        case 0x19: // P - пауза
            game.paused = !game.paused;
            tetris_draw_ui();
            break;
        case 0x01: // ESC - выход
            game.game_over = 1;
            break;
    }
}

void tetris_draw_game_over(void) {
    clear_screen();
    
    set_cursor(30, 8);
    printf("GAME OVER");
    set_cursor(30, 10);
    printf("Final Score: %d", game.score);
    set_cursor(30, 11);
    printf("Level: %d", game.level);
    set_cursor(30, 12);
    printf("Lines: %d", game.lines_cleared);
    set_cursor(30, 14);
    printf("Press any key...");
    
    // Ждем нажатия любой клавиши
    while (!keyboard_has_data()) {
        for (volatile int i = 0; i < 10000; i++);
    }
    
    // Считываем клавишу чтобы очистить буфер
    keyboard_read_scancode();
}

void tetris_game_loop(void) {
    tetris_init();
    
    // Начальная отрисовка
    tetris_draw_game();
    
    while (!game.game_over) {
        // Обрабатываем ввод
        tetris_handle_input();
        
        if (!game.paused) {
            // Автоматическое падение
            unsigned long current_time = get_game_timer();
            if (current_time - game.last_drop_time > tetris_get_drop_speed()) {
                tetris_move_piece(0, 1);
                game.last_drop_time = current_time;
            }
        }
        
        // Небольшая задержка для стабильности
        for (volatile int i = 0; i < 1000; i++);
    }
    
    tetris_draw_game_over();
}