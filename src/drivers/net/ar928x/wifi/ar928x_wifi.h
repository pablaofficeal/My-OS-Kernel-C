#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ar928x.h"
bool ar928x_wifi_scan(void *ctx);
bool ar928x_wifi_connect(void *ctx, const char *ssid, const char *password);
bool ar928x_wifi_disconnect(void *ctx);
void ar928x_wifi_poll(void *ctx, uint64_t now_ms);
bool ar928x_wifi_is_connected(void *ctx);
const struct wifi_ops *ar928x_wifi_ops_ptr(void);
