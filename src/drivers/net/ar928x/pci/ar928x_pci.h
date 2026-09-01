#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ar928x.h"
bool ar928x_pci_probe(void);
bool ar928x_pci_enable(void);
uint64_t ar928x_pci_bar_address(void);
void ar928x_pci_ensure_power(void);
bool ar928x_pci_is_known_id(uint16_t did);
