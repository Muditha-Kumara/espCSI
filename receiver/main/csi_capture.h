/*
 * CSI Capture - Receiver
 * Captures Wi-Fi Channel State Information from received frames
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CSI data callback type
 * @param csv_line  Formatted CSV line (null-terminated)
 * @param len       Length of csv_line
 */
typedef void (*csi_data_cb_t)(const char *csv_line, int len);

/**
 * @brief Initialize CSI capture subsystem
 * @return ESP_OK on success
 */
esp_err_t csi_capture_init(void);

/**
 * @brief Start CSI capture on a given channel
 * @param channel  WiFi channel (1-13)
 * @param cb       Callback invoked for each CSI frame
 * @return ESP_OK on success
 */
esp_err_t csi_capture_start(uint8_t channel, csi_data_cb_t cb);

/**
 * @brief Restrict capture to frames from a specific source MAC.
 *
 * ESP-NOW beacons from the broadcaster are the intended CSI source; other
 * Wi-Fi traffic on the same channel would pollute the dataset. Pass NULL
 * to capture from any source (default).
 *
 * @param mac  6-byte source MAC address, or NULL to disable filtering
 */
void csi_capture_set_source_mac(const uint8_t *mac);

/**
 * @brief Stop CSI capture
 */
void csi_capture_stop(void);

/**
 * @brief Check if CSI capture is active
 * @return true if capturing
 */
bool csi_capture_is_active(void);

/**
 * @brief Get count of captured frames in current session
 * @return Number of frames captured
 */
uint32_t csi_capture_get_count(void);

#ifdef __cplusplus
}
#endif
