// shell.h - Graphics-based shell header
#ifndef SHELL_H
#define SHELL_H

#include "../drivers/screen.h"

// Shell initialization and main loop
void init_shell();
void shell_run();

// Command execution
void execute_command(char *input);

// Command implementations
void cmd_help();
void cmd_clear();
void cmd_echo(char *args);
void cmd_version();
void cmd_reboot();
void cmd_shutdown();
void cmd_test(char *args);
void cmd_keytest();
void cmd_ls();
void cmd_cat(char *filename);
void cmd_touch(char *filename);
void cmd_rm(char *filename);
void cmd_fsinfo();
void cmd_df();
void cmd_format(char *args);
void cmd_create_test_files();
void cmd_write(char *args);
void cmd_info(char *filename);
void cmd_rename(char *args);
void cmd_space();
void cmd_edit(char *filename);
void cmd_snake(char *args);
void cmd_tetris(char *args);

void cmd_wifi(char *args);
void cmd_graphics(char *args);
void cmd_textmode(char *args);
void cmd_desktop(char *args);
void cmd_hexedit(char *args);

#endif