#pragma once
#include <stdint.h>
#include <stdbool.h>

void installer_run(const char *args);
bool installer_is_active(void);
void installer_handle_key(char c);
