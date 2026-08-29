#include "shell.h"
#include "window.h"
#include "../../libc/include/purec.h"

void _start(void){
    struct terminal_window terminal;
    if(!terminal_window_init(&terminal)) pc_exit(1);
    int status=shell_run(&terminal);
    terminal_window_shutdown(&terminal);
    pc_exit(status);
}
