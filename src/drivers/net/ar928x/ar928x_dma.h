#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ar928x.h"
bool ar928x_dma_allocate(void);
void ar928x_dma_release(void);
bool ar928x_dma_init_hw(void);
