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

#ifdef __cplusplus
}
#endif
