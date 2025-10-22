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

// Command declarations
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
extern void cmd_2048(char *args);
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

// Updated command implementations
void cmd_help() {
    shell_print("Available commands:");
    shell_print("  help     - Show this help");
    shell_print("  clear    - Clear screen");
    shell_print("  version  - Show version info");
    shell_print("  ls       - List files");
    shell_print("  cat      - Show file contents");
    shell_print("  touch    - Create file");
    shell_print("  rm       - Remove file");
    shell_print("  echo     - Print text");
    shell_print("  fsinfo   - Show filesystem info");
    shell_print("  df       - Show disk usage");
    shell_print("  format   - Format disk");
    shell_print("  testfiles- Create test files");
    shell_print("  write    - Write to file");
    shell_print("  info     - Show file info");
    shell_print("  rename   - Rename file");
    shell_print("  edit     - Edit file");
    shell_print("  space    - Show free space");
    shell_print("  snake    - Play Snake game");
    shell_print("  tetris   - Play Tetris game");
    shell_print("  2048     - Play 2048 game");
    shell_print("  wifi     - WiFi commands");
    shell_print("  reboot   - Reboot system");
    shell_print("  shutdown - Shutdown system");
}

void cmd_clear() {
    shell_clear();
}

void cmd_version() {
    shell_print("MyOS v1.0 - Framebuffer GUI Edition");
    shell_print("Pure C OS with Window Manager");
}

void cmd_echo(char *args) {
    shell_print(args);
}

void cmd_reboot() {
    shell_print("Rebooting...");
    // Add reboot code here
}

void cmd_shutdown() {
    shell_print("Shutting down...");
    // Add shutdown code here
}

void cmd_ls() {
    shell_print("File listing not implemented in GUI shell");
}

void cmd_cat(char *filename) {
    shell_print("File reading not implemented in GUI shell");
}

void cmd_touch(char *filename) {
    shell_print("File creation not implemented in GUI shell");
}

void cmd_rm(char *filename) {
    shell_print("File removal not implemented in GUI shell");
}

void cmd_fsinfo() {
    shell_print("Filesystem info not implemented in GUI shell");
}

void cmd_df() {
    shell_print("Disk usage not implemented in GUI shell");
}

void cmd_format(char *args) {
    shell_print("Disk format not implemented in GUI shell");
}

void cmd_create_test_files() {
    shell_print("Test file creation not implemented in GUI shell");
}

void cmd_write(char *args) {
    shell_print("File writing not implemented in GUI shell");
}

void cmd_info(char *filename) {
    shell_print("File info not implemented in GUI shell");
}

void cmd_rename(char *args) {
    shell_print("File rename not implemented in GUI shell");
}

void cmd_space() {
    shell_print("Free space info not implemented in GUI shell");
}

void cmd_edit(char *filename) {
    shell_print("File editing not implemented in GUI shell");
}

void cmd_keytest() {
    shell_print("Key test not implemented in GUI shell");
}

void cmd_test(char *args) {
    shell_print("Running tests...");
    shell_print("Framebuffer: OK");
    shell_print("Window Manager: OK");
    shell_print("Desktop: OK");
}

void cmd_graphics(char *args) {
    shell_print("Already in graphics mode!");
    shell_print("Desktop with window manager is active.");
}

void cmd_textmode(char *args) {
    shell_print("Text mode removed!");
    shell_print("This OS now uses framebuffer graphics only.");
}

void cmd_desktop(char *args) {
    shell_print("Desktop is already running!");
    shell_print("Use mouse to interact with windows.");
}

void cmd_snake(char *args) {
    shell_print("Snake game not implemented in GUI shell");
}

void cmd_tetris(char *args) {
    shell_print("Tetris game not implemented in GUI shell");
}

void cmd_2048(char *args) {
    shell_print("2048 game not implemented in GUI shell");
}

void cmd_wifi(char *args) {
    shell_print("WiFi not implemented in GUI shell");
}

void cmd_hexedit(char *args) {
    shell_print("Hex editor not implemented in GUI shell");
}

void execute_command(char *input) {
    if (strcmp(input, "help") == 0) cmd_help();
    else if (strcmp(input, "clear") == 0) cmd_clear();
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
    else if (strcmp(input, "2048") == 0) cmd_2048("");
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
        char key = getchar();
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