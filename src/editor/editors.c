#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>

#define MAX_SIZE 65536
#define LINE_WIDTH 16

typedef struct {
    unsigned char data[MAX_SIZE];
    size_t size;
    size_t cursor;
    int mode; // 0 = hex, 1 = ascii
    char filename[256];
} Editor;

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void initEditor(Editor *editor, const char *filename) {
    editor->size = 0;
    editor->cursor = 0;
    editor->mode = 0;
    strcpy(editor->filename, filename);
    
    // Попробуем загрузить существующий файл
    FILE *file = fopen(filename, "rb");
    if (file) {
        editor->size = fread(editor->data, 1, MAX_SIZE, file);
        fclose(file);
    }
}

void saveFile(Editor *editor) {
    FILE *file = fopen(editor->filename, "wb");
    if (file) {
        fwrite(editor->data, 1, editor->size, file);
        fclose(file);
        printf("\nФайл сохранен: %s\n", editor->filename);
    } else {
        printf("\nОшибка сохранения файла!\n");
    }
}

void displayHex(Editor *editor) {
    system("clear");
    printf("Hex Editor - %s | Режим: %s | Размер: %zu байт\n", 
           editor->filename, 
           editor->mode ? "ASCII" : "HEX", 
           editor->size);
    printf("Управление: TAB-режим, Ctrl+S-сохранить, Ctrl+Q-выход\n\n");
    
    for (size_t i = 0; i < editor->size; i += LINE_WIDTH) {
        // Адрес
        printf("%08zx: ", i);
        
        // Hex данные
        for (int j = 0; j < LINE_WIDTH; j++) {
            if (i + j < editor->size) {
                if (i + j == editor->cursor) {
                    printf("\033[41m%02x\033[0m ", editor->data[i + j]);
                } else {
                    printf("%02x ", editor->data[i + j]);
                }
            } else {
                printf("   ");
            }
        }
        
        printf(" ");
        
        // ASCII представление
        for (int j = 0; j < LINE_WIDTH; j++) {
            if (i + j < editor->size) {
                unsigned char c = editor->data[i + j];
                if (i + j == editor->cursor) {
                    printf("\033[41m%c\033[0m", isprint(c) ? c : '.');
                } else {
                    printf("%c", isprint(c) ? c : '.');
                }
            } else {
                printf(" ");
            }
        }
        printf("\n");
    }
    
    // Если файл пустой
    if (editor->size == 0) {
        printf("00000000: \033[41m  \033[0m\n");
    }
}

void handleInput(Editor *editor) {
    char c;
    read(STDIN_FILENO, &c, 1);
    
    switch (c) {
        case 9: // TAB - переключение режима
            editor->mode = !editor->mode;
            break;
            
        case 1: // Ctrl+A - добавить байт
            if (editor->size < MAX_SIZE) {
                memmove(&editor->data[editor->cursor + 1], 
                       &editor->data[editor->cursor], 
                       editor->size - editor->cursor);
                editor->data[editor->cursor] = 0x00;
                editor->size++;
            }
            break;
            
        case 4: // Ctrl+D - удалить байт
            if (editor->size > 0 && editor->cursor < editor->size) {
                memmove(&editor->data[editor->cursor], 
                       &editor->data[editor->cursor + 1], 
                       editor->size - editor->cursor - 1);
                editor->size--;
                if (editor->cursor >= editor->size && editor->size > 0) {
                    editor->cursor = editor->size - 1;
                }
            }
            break;
            
        case 19: // Ctrl+S - сохранить
            saveFile(editor);
            usleep(1000000); // Пауза 1 сек
            break;
            
        case 17: // Ctrl+Q - выход
            printf("\nВыход...\n");
            exit(0);
            break;
            
        case 27: // Стрелки
            {
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) != 1) return;
                if (read(STDIN_FILENO, &seq[1], 1) != 1) return;
                
                switch(seq[1]) {
                    case 'A': // Вверх
                        if (editor->cursor >= LINE_WIDTH)
                            editor->cursor -= LINE_WIDTH;
                        break;
                    case 'B': // Вниз
                        if (editor->cursor + LINE_WIDTH < editor->size)
                            editor->cursor += LINE_WIDTH;
                        break;
                    case 'C': // Вправо
                        if (editor->cursor < editor->size - 1)
                            editor->cursor++;
                        break;
                    case 'D': // Влево
                        if (editor->cursor > 0)
                            editor->cursor--;
                        break;
                }
            }
            break;
            
        default:
            if (editor->mode == 0) { // Hex режим
                if (isxdigit(c) && editor->cursor < MAX_SIZE) {
                    // Простая реализация - ждем второй hex-символ
                    static char hex[3] = {0};
                    static int hex_pos = 0;
                    
                    hex[hex_pos++] = c;
                    if (hex_pos == 2) {
                        unsigned char value = strtol(hex, NULL, 16);
                        if (editor->cursor >= editor->size) {
                            editor->size++;
                        }
                        editor->data[editor->cursor] = value;
                        hex_pos = 0;
                        if (editor->cursor < MAX_SIZE - 1) {
                            editor->cursor++;
                        }
                    }
                }
            } else { // ASCII режим
                if (isprint(c) && editor->cursor < MAX_SIZE) {
                    if (editor->cursor >= editor->size) {
                        editor->size++;
                    }
                    editor->data[editor->cursor] = c;
                    if (editor->cursor < MAX_SIZE - 1) {
                        editor->cursor++;
                    }
                }
            }
            break;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Использование: %s <filename>\n", argv[0]);
        return 1;
    }
    
    Editor editor;
    initEditor(&editor, argv[1]);
    enableRawMode();
    
    while (1) {
        displayHex(&editor);
        handleInput(&editor);
    }
    
    return 0;
}