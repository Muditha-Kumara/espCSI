/*
 * Web Server - Receiver
 * HTTP server for web UI, session control, and file download
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the HTTP web server
 * @return ESP_OK on success
 */
esp_err_t web_server_init(void);

/**
 * @brief Stop the HTTP web server
 */
void web_server_stop(void);

/**
 * @brief Programmatically stop the running session (same as POST stop).
 *        No-op when no session is running. Safe to call from other tasks.
 */
void web_server_stop_session(void);

/**
 * @brief Query whether a session is currently running
 */
bool web_server_session_is_running(void);

/**
 * @brief Seconds elapsed in the current session (0 when idle)
 */
uint32_t web_server_session_elapsed_s(void);

/**
 * @brief Configured duration of the current session in seconds
 */
uint32_t web_server_session_duration_s(void);

#ifdef __cplusplus
}
#endif
