/*
 * ESP-NOW Control - Broadcaster
 * Listens for commands from receiver, sends announcements
 */

#pragma once

#include "esp_err.h"
#include "espnow_proto.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback when START command is received
 */
typedef void (*espnow_start_cb_t)(const espnow_start_t *msg);

/**
 * @brief Callback when STOP command is received
 */
typedef void (*espnow_stop_cb_t)(const espnow_stop_t *msg);

/**
 * @brief Callback when CONFIG command is received
 */
typedef void (*espnow_config_cb_t)(const espnow_config_t *msg);

esp_err_t espnow_ctrl_init(void);
void espnow_ctrl_on_start(espnow_start_cb_t cb);
void espnow_ctrl_on_stop(espnow_stop_cb_t cb);
void espnow_ctrl_on_config(espnow_config_cb_t cb);

/**
 * @brief Start periodic announce task (every 5s when idle)
 */
esp_err_t espnow_ctrl_start_announce(void);

/**
 * @brief Stop periodic announce
 */
void espnow_ctrl_stop_announce(void);

/**
 * @brief Send ACK to receiver
 */
esp_err_t espnow_ctrl_send_ack(const uint8_t *dest_mac, uint8_t ack_cmd, uint8_t status);

#ifdef __cplusplus
}
#endif
