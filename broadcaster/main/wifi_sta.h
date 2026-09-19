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
uint8_t wifi_sta_get_channel(void);
esp_err_t wifi_sta_set_channel(uint8_t channel);
esp_err_t wifi_sta_restore_channel(void);

/**
 * @brief Suppress automatic reconnection after a channel switch.
 *
 * esp_wifi_set_channel() disconnects the STA; the event handler would
 * normally reconnect, pulling the radio back to the home AP channel and
 * breaking the ESP-NOW session with the receiver.
 *
 * @param suppress  true = do not auto-reconnect on disconnect
 */
void wifi_sta_set_reconnect_suppressed(bool suppress);

#ifdef __cplusplus
}
#endif
