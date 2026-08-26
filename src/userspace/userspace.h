#pragma once
#include <stdint.h>
#include <stdbool.h>

// примитивный userspace: рабочий стол + терминал, как в Linux после boot splash
void userspace_init(void);
void userspace_run(void); // never returns

// для shell команд
void userspace_exec_command(const char *cmd);
