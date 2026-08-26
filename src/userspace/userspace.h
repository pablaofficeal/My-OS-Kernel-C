#pragma once
#include <stdint.h>
#include <stdbool.h>

void userspace_init(void);
void userspace_run(void);

void userspace_exec_command(const char *cmd);