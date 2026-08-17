/*
 * Wi-Fi Station Mode - Receiver
 * Manages Wi-Fi STA connection to home network
 */

#pragma once

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Wi-Fi in station mode and connect
 * @return ESP_OK on success
 */
esp_err_t wifi_sta_init(void);

/**
 * @brief Wait for Wi-Fi connection (blocking)
 * @param timeout_ms Maximum wait time in milliseconds
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT otherwise
 */
esp_err_t wifi_sta_wait_connect(int timeout_ms);

/**
 * @brief Check if Wi-Fi is currently connected
 * @return true if connected
 */
bool wifi_sta_is_connected(void);

/**
 * @brief Disconnect Wi-Fi
 */
void receiver_wifi_sta_disconnect(void);

/**
 * @brief Reconnect Wi-Fi
 */
void receiver_wifi_sta_reconnect(void);

/**
 * @brief Get current Wi-Fi channel
 * @return Channel number (1-13) or 0 if not connected
 */
uint8_t wifi_sta_get_channel(void);

/**
 * @brief Set Wi-Fi to a specific channel (for CSI collection)
 * @param channel Channel number (1-13)
 * @return ESP_OK on success
 */
esp_err_t wifi_sta_set_channel(uint8_t channel);

/**
 * @brief Restore Wi-Fi to home AP channel
 * @return ESP_OK on success
 */
esp_err_t wifi_sta_restore_channel(void);

#ifdef __cplusplus
}
#endif
