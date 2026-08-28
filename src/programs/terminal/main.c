#include "shell.h"
#include "../../libc/include/purec.h"

void _start(void){
    pc_exit(shell_run());
}
