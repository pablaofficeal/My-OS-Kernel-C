#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ar928x.h"
uint32_t ar928x_reg_read(uint32_t off);
void ar928x_reg_write(uint32_t off, uint32_t v);
void ar928x_hw_read_srev(void);
bool ar928x_hw_reset(void);
void ar928x_hw_disable_interrupts(void);
bool ar928x_hw_check_ready(void);
