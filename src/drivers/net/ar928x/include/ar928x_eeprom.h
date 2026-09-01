#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ar928x.h"
bool ar928x_eeprom_read_mac(uint8_t out[6]);
bool ar928x_eeprom_valid_mac(const uint8_t m[6]);
bool ar928x_eeprom_try_read(uint8_t out[6]);
