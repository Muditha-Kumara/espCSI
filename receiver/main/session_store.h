/*
 * Session Store - Receiver
 * Manages CSV session files on FATFS storage partition
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SESSION_LABEL_MAX  32
#define SESSION_ID_LEN     16

/**
 * @brief Initialize session store (mount FATFS)
 * @return ESP_OK on success
 */
esp_err_t session_store_init(void);

/**
 * @brief Begin a new recording session
 * @param label    Activity label (e.g. "walking")
 * @param channel  WiFi channel used
 * @param rate_hz  Broadcast rate in Hz
 * @return ESP_OK on success
 */
esp_err_t session_store_begin(const char *label, uint8_t channel, uint16_t rate_hz);

/**
 * @brief Append a CSV line to the current session file
 * @param csv_line  CSV data line
 * @param len       Length of csv_line
 * @return ESP_OK on success
 */
esp_err_t session_store_append(const char *csv_line, int len);

/**
 * @brief Flush and close the current session file
 * @return ESP_OK on success
 */
esp_err_t session_store_end(void);

/**
 * @brief Check if a session is currently open
 * @return true if recording
 */
bool session_store_is_active(void);

/**
 * @brief Get the current session ID
 * @return Pointer to session ID string (16 bytes), or NULL if not active
 */
const char *session_store_get_id(void);

/**
 * @brief List all stored session files
 * @param buf       Output buffer for JSON array
 * @param buf_size  Size of buf
 * @return ESP_OK on success
 */
esp_err_t session_store_list(char *buf, size_t buf_size);

/**
 * @brief Delete a session file by name
 * @param filename  Session filename to delete
 * @return ESP_OK on success
 */
esp_err_t session_store_delete(const char *filename);

/**
 * @brief Get the path to a session file for download
 * @param filename  Session filename
 * @param path      Output buffer for full path
 * @param path_size Size of path buffer
 * @return ESP_OK on success
 */
esp_err_t session_store_get_path(const char *filename, char *path, size_t path_size);

/**
 * @brief Get free storage space in bytes
 * @return Free bytes available
 */
uint64_t session_store_get_free_space(void);

#ifdef __cplusplus
}
#endif
