/*
 * Beacon TX - Broadcaster
 * Sends ESP-NOW frames at configurable rate for CSI generation
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize beacon TX subsystem
 * @return ESP_OK on success
 */
esp_err_t beacon_tx_init(void);

/**
 * @brief Start transmitting beacons at given rate
 * @param rate_hz  Transmission rate in Hz (10-200)
 * @param duration_s  Duration in seconds (0 = indefinite)
 * @return ESP_OK on success
 */
esp_err_t beacon_tx_start(uint16_t rate_hz, uint32_t duration_s);

/**
 * @brief Stop transmitting beacons
 */
void beacon_tx_stop(void);

/**
 * @brief Check if beacon TX is active
 * @return true if transmitting
 */
bool beacon_tx_is_active(void);

/**
 * @brief Get count of transmitted beacons
 * @return Number of beacons sent
 */
uint32_t beacon_tx_get_count(void);

#ifdef __cplusplus
}
#endif
