#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ar928x.h"
bool ar928x_mac_setup(void);
bool ar928x_mac_program(const uint8_t mac[6]);
void ar928x_mac_generate_fallback(uint8_t out[6]);
