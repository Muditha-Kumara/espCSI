/*
 * OTA Client - Receiver
 * Handles over-the-air firmware updates from local server
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA state
 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_DONE,
    OTA_STATE_ERROR,
} ota_state_t;

/**
 * @brief Initialize OTA client
 * @return ESP_OK on success
 */
esp_err_t ota_client_init(void);

/**
 * @brief Start periodic OTA check task (every 30 min when idle)
 * @return ESP_OK on success
 */
esp_err_t ota_client_start_periodic(void);

/**
 * @brief Trigger a manual OTA check and update
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already in progress
 */
esp_err_t ota_client_check_now(void);

/**
 * @brief Get current OTA state
 * @return Current OTA state
 */
ota_state_t ota_client_get_state(void);

/**
 * @brief Get OTA status message
 * @return Status string (static storage)
 */
const char *ota_client_get_status(void);

/**
 * @brief Check if OTA is currently in progress
 * @return true if downloading
 */
bool ota_client_is_active(void);

#ifdef __cplusplus
}
#endif
