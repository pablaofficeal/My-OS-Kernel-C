#ifndef TETRIS_H
#define TETRIS_H

#include "../../drivers/screen.h"
#include "../../drivers/keyboard/keyboard.h"

#define TETRIS_WIDTH 10
#define TETRIS_HEIGHT 20
#define TETRIS_VISIBLE_HEIGHT 20

// Размеры игрового поля (с границами)
#define FIELD_WIDTH (TETRIS_WIDTH + 2)
#define FIELD_HEIGHT (TETRIS_HEIGHT + 1)

// Цвета для тетрамино (ANSI коды)
#define COLOR_CYAN    "36"
#define COLOR_BLUE    "34"
#define COLOR_ORANGE  "33"
#define COLOR_YELLOW  "33"
#define COLOR_GREEN   "32"
#define COLOR_PURPLE  "35"
#define COLOR_RED     "31"
#define COLOR_WHITE   "37"

// Типы тетрамино
typedef enum {
    TETRIS_I = 0,
    TETRIS_J,
    TETRIS_L,
    TETRIS_O,
    TETRIS_S,
    TETRIS_T,
    TETRIS_Z,
    TETRIS_COUNT
} tetris_piece_t;

// Структура для тетрамино
typedef struct {
    tetris_piece_t type;
    int x, y;           // Позиция на поле
    int rotation;       // Текущий поворот (0-3)
    int shape[4][4];    // Форма фигуры
    const char* color;
} tetris_piece;

// Структура игры
typedef struct {
    int field[FIELD_HEIGHT][FIELD_WIDTH];  // Игровое поле
    tetris_piece current_piece;            // Текущая фигура
    tetris_piece next_piece;               // Следующая фигура
    int score;
    int level;
    int lines_cleared;
    int game_over;
    int paused;
    unsigned long last_drop_time;
} tetris_game;

// Функции игры
void tetris_init(void);
void tetris_game_loop(void);
void tetris_draw_game(void);
void tetris_handle_input(void);
void tetris_move_piece(int dx, int dy);
void tetris_rotate_piece(void);
void tetris_drop_piece(void);
int tetris_check_collision(void);
void tetris_lock_piece(void);
int tetris_clear_lines(void);
void tetris_spawn_piece(void);
void tetris_draw_piece(const tetris_piece* piece, int draw);
void tetris_draw_field(void);
void tetris_draw_ui(void);
void tetris_draw_game_over(void);
unsigned long tetris_get_drop_speed(void);

#endif