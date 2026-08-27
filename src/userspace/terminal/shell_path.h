#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SHELL_PATH_CAPACITY 128

const char *shell_path_current(void);
bool shell_path_resolve(const char *path, char output[SHELL_PATH_CAPACITY]);
bool shell_path_change(const char *path);
