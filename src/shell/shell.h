// shell.h - ДОБАВИТЬ новые объявления
#ifndef SHELL_H
#define SHELL_H

void execute_command(char *input);
void shell_init();
void shell_run();

// Все команды
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

// НОВЫЕ КОМАНДЫ - ДОБАВИТЬ ЭТИ СТРОКИ:
void cmd_write(char *args);
void cmd_info(char *filename);
void cmd_rename(char *args);
void cmd_space();

#endif