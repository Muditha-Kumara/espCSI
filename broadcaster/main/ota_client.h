/*
 * OTA Client - Broadcaster
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_DONE,
    OTA_STATE_ERROR,
} ota_state_t;

esp_err_t ota_client_init(void);
esp_err_t ota_client_start_periodic(void);
esp_err_t ota_client_check_now(void);
ota_state_t ota_client_get_state(void);
const char *ota_client_get_status(void);
bool ota_client_is_active(void);

#ifdef __cplusplus
}
#endif
