#pragma once

#include <stdbool.h>

void nano_open(const char *path);
bool nano_is_active(void);
void nano_handle_key(char character);
