#pragma once
#include <stdbool.h>

bool intel_ax201_init(void);
bool intel_ax201_has_hardware(void);
const char *intel_ax201_hardware_info(void);
