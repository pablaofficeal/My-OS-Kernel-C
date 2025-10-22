// shell.c - Graphics-based shell for framebuffer GUI
#include "shell.h"
#include "../drivers/screen.h"
#include "../drivers/keyboard/keyboard.h"
#include "../lib/string.h"
#include "../fs/fat16.h"
#include "../tools/hexedit.h"

// Global desktop reference
static desktop_t* shell_desktop = 0;
static window_t* shell_window = 0;
static int shell_cursor_x = 20;
static int shell_cursor_y = 50;

// External function declarations
extern char keyboard_getchar();

// External command functions from commands.c
extern void cmd_help();
extern void cmd_clear();
extern void cmd_echo(char *args);
extern void cmd_version();
extern void cmd_reboot();
extern void cmd_shutdown();
extern void cmd_test(char *args);
extern void cmd_keytest();
extern void cmd_ls();
extern void cmd_cat(char *filename);
extern void cmd_touch(char *filename);
extern void cmd_rm(char *filename);
extern void cmd_fsinfo();
extern void cmd_df();
extern void cmd_format(char *args);
extern void cmd_create_test_files();
extern void cmd_write(char *args);
extern void cmd_info(char *filename);
extern void cmd_rename(char *args);
extern void cmd_space();
extern void cmd_edit(char *filename);
extern void cmd_snake(char *args);
extern void cmd_wifi(char *args);
extern void cmd_tetris(char *args);

extern void cmd_graphics(char *args);
extern void cmd_textmode(char *args);
extern void cmd_desktop(char *args);
extern void cmd_hexedit(char *args);

// Shell helper functions
void shell_print(const char* text) {
    if (shell_window) {
        draw_string(shell_cursor_x, shell_cursor_y, text, COLOR_WHITE);
        shell_cursor_y += 16; // Move to next line
        
        // Scroll if needed
        if (shell_cursor_y > shell_window->height - 30) {
            // Simple scroll: copy everything up by 16 pixels
            for (int y = 50; y < shell_window->height - 16; y++) {
                for (int x = 20; x < shell_window->width - 20; x++) {
                    uint32_t pixel = framebuffer_get_pixel(shell_window->x + x, shell_window->y + y + 16);
                    shell_window->buffer[y * shell_window->width + x] = pixel;
                }
            }
            // Clear bottom line
            for (int y = shell_window->height - 16; y < shell_window->height; y++) {
                for (int x = 20; x < shell_window->width - 20; x++) {
                    shell_window->buffer[y * shell_window->width + x] = COLOR_TRANSPARENT;
                }
            }
            shell_cursor_y = shell_window->height - 16;
            window_paint(shell_window);
        }
    }
}

void shell_clear() {
    if (shell_window) {
        // Clear window content area
        for (int y = 50; y < shell_window->height - 20; y++) {
            for (int x = 20; x < shell_window->width - 20; x++) {
                shell_window->buffer[y * shell_window->width + x] = COLOR_TRANSPARENT;
            }
        }
        shell_cursor_x = 20;
        shell_cursor_y = 50;
        window_paint(shell_window);
    }
}

void shell_prompt() {
    shell_print("FAT16> ");
    shell_cursor_x = 20 + 7 * 8; // Position after prompt
}

void execute_command(char *input) {
    if (strcmp(input, "help") == 0) cmd_help();
    else if (strcmp(input, "clear") == 0) { shell_clear(); cmd_clear(); }
    else if (strcmp(input, "version") == 0) cmd_version();
    else if (strcmp(input, "reboot") == 0) cmd_reboot();
    else if (strcmp(input, "shutdown") == 0) cmd_shutdown();
    else if (strcmp(input, "test") == 0) cmd_test("");
    else if (strcmp(input, "keytest") == 0) cmd_keytest();
    else if (strcmp(input, "ls") == 0) cmd_ls();
    else if (strncmp(input, "cat ", 4) == 0) cmd_cat(input + 4);
    else if (strncmp(input, "touch ", 6) == 0) cmd_touch(input + 6);
    else if (strncmp(input, "rm ", 3) == 0) cmd_rm(input + 3);
    else if (strcmp(input, "fsinfo") == 0) cmd_fsinfo();
    else if (strcmp(input, "df") == 0) cmd_df();
    else if (strcmp(input, "format") == 0) cmd_format("");
    else if (strcmp(input, "testfiles") == 0) cmd_create_test_files();
    else if (strncmp(input, "echo ", 5) == 0) cmd_echo(input + 5);
    else if (strncmp(input, "write ", 6) == 0) cmd_write(input + 6);
    else if (strncmp(input, "info ", 5) == 0) cmd_info(input + 5);
    else if (strncmp(input, "rename ", 7) == 0) cmd_rename(input + 7);
    else if (strncmp(input, "edit ", 5) == 0) cmd_edit(input + 5);
    else if (strcmp(input, "space") == 0) cmd_space();
    else if (strcmp(input, "snake") == 0) cmd_snake("");
    else if (strncmp(input, "wifi ", 5) == 0) cmd_wifi(input + 5);
    else if (strcmp(input, "tetris") == 0) cmd_tetris("");

    else if (strcmp(input, "graphics") == 0) cmd_graphics("");
    else if (strcmp(input, "textmode") == 0) cmd_textmode("");
    else if (strcmp(input, "desktop") == 0) cmd_desktop("");
    else if (strncmp(input, "hexedit", 7) == 0) {
        if (input[7] == ' ') {
            cmd_hexedit(input + 8);
        } else {
            cmd_hexedit("");
        }
    }
    else if (input[0] != '\0') {
        shell_print("Unknown command: ");
        shell_print(input);
    }
}

void init_shell() {
    // Get desktop from kernel
    shell_desktop = global_desktop;
    if (!shell_desktop) {
        return;
    }
    
    // Create shell window
    shell_window = create_window(50, 50, 600, 400, "Shell", WINDOW_FLAG_DECORATED);
    if (!shell_window) {
        return;
    }
    
    // Show welcome message
    draw_string(20, 30, "MyOS Shell - Graphics Mode", COLOR_WHITE);
    draw_string(20, 40, "================================", COLOR_WHITE);
    shell_prompt();
    
    window_paint(shell_window);
}

void shell_run() {
    char input_buffer[256];
    int input_pos = 0;
    
    while (1) {
        char key = keyboard_getchar();
        if (key) {
            if (key == '\n') {
                input_buffer[input_pos] = '\0';
                shell_cursor_x = 20;
                shell_print(""); // New line
                execute_command(input_buffer);
                input_pos = 0;
                shell_prompt();
            } else if (key == '\b' && input_pos > 0) {
                input_pos--;
                // Simple backspace - redraw line
                shell_cursor_x = 20 + 7 * 8 + input_pos * 8;
                draw_char(shell_cursor_x, shell_cursor_y, ' ', COLOR_WHITE);
            } else if (key >= 32 && key <= 126 && input_pos < 255) {
                input_buffer[input_pos++] = key;
                draw_char(shell_cursor_x, shell_cursor_y, key, COLOR_WHITE);
                shell_cursor_x += 8;
            }
            window_paint(shell_window);
        }
    }
}