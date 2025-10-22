// src/game/snake.c
#include "snake.h"
#include "../../drivers/text_output.h"

// Function declarations
void set_cursor(int x, int y);
void clear_screen(void);

static snake_t snake;
static food_t food;
static int game_running = 0;
static unsigned long last_move_time = 0;

// Объявляем функции которые будем использовать
extern int keyboard_has_data(void);
extern unsigned char keyboard_read_scancode(void);

// ПЕРЕНЕСЕМ get_system_timer сюда
static unsigned long system_timer_counter = 0;

static unsigned long get_system_timer(void) {
    return system_timer_counter++;
}

void snake_init(void) {
    // Инициализация змейки
    snake.length = 3;
    snake.direction = SNAKE_RIGHT;
    snake.score = 0;
    
    // Начальная позиция змейки
    for (int i = 0; i < snake.length; i++) {
        snake.x[i] = 10 - i;
        snake.y[i] = 12;
    }
    
    // Инициализация еды
    food.active = 0;
    food.x = 0;
    food.y = 0;
    
    game_running = 1;
    last_move_time = 0;
    
    // Создаем первую еду
    snake_spawn_food();
}

void snake_draw_board(void) {
    // Очищаем экран
    clear_screen();
    
    // Рисуем границы
    for (int x = 0; x < SNAKE_WIDTH; x++) {
        set_cursor(x, 0);
        putchar('#');
        set_cursor(x, SNAKE_HEIGHT - 1);
        putchar('#');
    }
    
    for (int y = 0; y < SNAKE_HEIGHT; y++) {
        set_cursor(0, y);
        putchar('#');
        set_cursor(SNAKE_WIDTH - 1, y);
        putchar('#');
    }
    
    // Рисуем счет
    set_cursor(2, 1);
    printf("Score: %d", snake.score);
    set_cursor(2, 2);
    printf("Length: %d", snake.length);
    
    // Инструкции
    set_cursor(SNAKE_WIDTH - 20, 1);
    printf("WASD - Move");
    set_cursor(SNAKE_WIDTH - 20, 2);
    printf("ESC - Quit");
}

void snake_draw_snake(void) {
    // Рисуем голову
    set_cursor(snake.x[0], snake.y[0]);
    putchar('@');  // Голова
    
    // Рисуем тело
    for (int i = 1; i < snake.length; i++) {
        set_cursor(snake.x[i], snake.y[i]);
        putchar('*');  // Тело
    }
}

void snake_draw_food(void) {
    if (food.active) {
        set_cursor(food.x, food.y);
        putchar('$');  // Еда
    }
}

void snake_move(void) {
    // Сохраняем старые координаты
    int prev_x[SNAKE_MAX_LENGTH];
    int prev_y[SNAKE_MAX_LENGTH];
    
    for (int i = 0; i < snake.length; i++) {
        prev_x[i] = snake.x[i];
        prev_y[i] = snake.y[i];
    }
    
    // Двигаем голову
    switch (snake.direction) {
        case SNAKE_UP:
            snake.y[0]--;
            break;
        case SNAKE_DOWN:
            snake.y[0]++;
            break;
        case SNAKE_LEFT:
            snake.x[0]--;
            break;
        case SNAKE_RIGHT:
            snake.x[0]++;
            break;
    }
    
    // Двигаем тело
    for (int i = 1; i < snake.length; i++) {
        snake.x[i] = prev_x[i - 1];
        snake.y[i] = prev_y[i - 1];
    }
    
    // Проверяем столкновение с едой
    if (food.active && snake.x[0] == food.x && snake.y[0] == food.y) {
        snake.score += 10;
        
        // Увеличиваем змейку
        if (snake.length < SNAKE_MAX_LENGTH) {
            snake.length++;
            snake.x[snake.length - 1] = prev_x[snake.length - 2];
            snake.y[snake.length - 1] = prev_y[snake.length - 2];
        }
        
        // Создаем новую еду
        snake_spawn_food();
    }
}

void snake_handle_input(void) {
    if (!keyboard_has_data()) return;
    
    unsigned char scancode = keyboard_read_scancode();
    
    // Обрабатываем только нажатия (без отпускания)
    if (scancode & 0x80) return;
    
    switch (scancode) {
        case 0x11: // W
            if (snake.direction != SNAKE_DOWN)
                snake.direction = SNAKE_UP;
            break;
        case 0x1F: // S
            if (snake.direction != SNAKE_UP)
                snake.direction = SNAKE_DOWN;
            break;
        case 0x1E: // A
            if (snake.direction != SNAKE_RIGHT)
                snake.direction = SNAKE_LEFT;
            break;
        case 0x20: // D
            if (snake.direction != SNAKE_LEFT)
                snake.direction = SNAKE_RIGHT;
            break;
        case 0x01: // ESC
            game_running = 0;
            break;
    }
}

void snake_spawn_food(void) {
    int valid_position = 0;
    
    while (!valid_position) {
        // Генерируем случайную позицию
        food.x = 2 + (get_system_timer() % (SNAKE_WIDTH - 4));
        food.y = 2 + (get_system_timer() % (SNAKE_HEIGHT - 4));
        
        // Проверяем что еда не на змейке
        valid_position = 1;
        for (int i = 0; i < snake.length; i++) {
            if (food.x == snake.x[i] && food.y == snake.y[i]) {
                valid_position = 0;
                break;
            }
        }
        
        // Проверяем что еда не на границе
        if (food.x <= 1 || food.x >= SNAKE_WIDTH - 2 ||
            food.y <= 1 || food.y >= SNAKE_HEIGHT - 2) {
            valid_position = 0;
        }
    }
    
    food.active = 1;
}

int snake_check_collision(void) {
    // Проверяем столкновение с границами
    if (snake.x[0] <= 1 || snake.x[0] >= SNAKE_WIDTH - 2 ||
        snake.y[0] <= 1 || snake.y[0] >= SNAKE_HEIGHT - 2) {
        return 1;
    }
    
    // Проверяем столкновение с собой
    for (int i = 1; i < snake.length; i++) {
        if (snake.x[0] == snake.x[i] && snake.y[0] == snake.y[i]) {
            return 1;
        }
    }
    
    return 0;
}

void snake_show_game_over(void) {
    clear_screen();
    
    set_cursor(35, 10);
    printf("GAME OVER");
    set_cursor(35, 12);
    printf("Score: %d", snake.score);
    set_cursor(35, 14);
    printf("Press any key...");
    
    // Ждем нажатия любой клавиши
    while (!keyboard_has_data()) {
        // Простая задержка
        for (volatile int i = 0; i < 10000; i++);
    }
    
    // Считываем клавишу чтобы очистить буфер
    keyboard_read_scancode();
}

void snake_game_loop(void) {
    snake_init();
    
    while (game_running) {
        // Обрабатываем ввод
        snake_handle_input();
        
        // Двигаем змейку каждые 100000 итераций (замедление игры)
        unsigned long current_time = get_system_timer();
        if (current_time - last_move_time > 100000) {
            snake_move();
            last_move_time = current_time;
            
            // Проверяем столкновения
            if (snake_check_collision()) {
                game_running = 0;
                break;
            }
            
            // Отрисовываем игру
            snake_draw_board();
            snake_draw_snake();
            snake_draw_food();
        }
        
        // Небольшая задержка для стабильности
        for (volatile int i = 0; i < 350; i++);
    }
    
    snake_show_game_over();
}