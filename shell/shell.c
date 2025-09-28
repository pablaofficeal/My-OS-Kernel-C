#include "shell.h"
#include "../drivers/screen.h"
#include "../drivers/keyboard.h"
#include "../lib/string.h"

// Объявления всех команд
extern void cmd_help();
extern void cmd_clear();
extern void cmd_echo(char *args);
extern void cmd_version();
extern void cmd_reboot();
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

// shell.c - ДОБАВИТЬ В execute_command
void execute_command(char *input) {
    if (strcmp(input, "help") == 0) cmd_help();
    else if (strcmp(input, "clear") == 0) cmd_clear();
    else if (strcmp(input, "version") == 0) cmd_version();
    else if (strcmp(input, "reboot") == 0) cmd_reboot();
    else if (strcmp(input, "shutdown") == 0) cmd_shutdown();  // <-- ДОБАВИТЬ ЭТУ СТРОКУ
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
    else if (strcmp(input, "space") == 0) cmd_space();
    else if (input[0] != '\0') printf("Unknown command: %s\n", input);
}

void shell_init() {
    clear_screen();
    printf("=========================================\n");
    printf("    PureC OS - FAT16 File System\n");
    printf("=========================================\n");
    printf("500MB Disk • FAT16 • Modular Kernel\n");
    printf("Type 'help' for available commands\n");
    printf("=========================================\n\n");
}

void shell_run() {
    char input_buffer[256];
    
    while (1) {
        printf("FAT16> ");
        readline(input_buffer, sizeof(input_buffer));
        execute_command(input_buffer);
    }
}