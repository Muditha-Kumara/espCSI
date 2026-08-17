/*
 * Wi-Fi Station Mode - Broadcaster
 * Connects to home Wi-Fi for OTA, switches channel for ESP-NOW TX
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_sta_init(void);
esp_err_t wifi_sta_wait_connect(int timeout_ms);
bool wifi_sta_is_connected(void);
void wifi_sta_disconnect(void);
uint8_t wifi_sta_get_channel(void);
esp_err_t wifi_sta_set_channel(uint8_t channel);
esp_err_t wifi_sta_restore_channel(void);

#ifdef __cplusplus
}
#endif
