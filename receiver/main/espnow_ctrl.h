/*
 * ESP-NOW Control - Receiver
 * Handles ESP-NOW communication with broadcaster device
 */

#pragma once

#include "esp_err.h"
#include "espnow_proto.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback when broadcaster announces itself
 * @param mac      Broadcaster MAC address
 * @param channel  Broadcaster's current channel
 * @param version  Broadcaster firmware version
 */
typedef void (*espnow_announce_cb_t)(const uint8_t *mac, uint8_t channel, const char *version);

/**
 * @brief Callback when broadcaster sends ACK
 * @param ack_cmd  Command being acknowledged
 * @param status   0 = success, 1 = error
 */
typedef void (*espnow_ack_cb_t)(uint8_t ack_cmd, uint8_t status);

/**
 * @brief Initialize ESP-NOW control
 * @return ESP_OK on success
 */
esp_err_t espnow_ctrl_init(void);

/**
 * @brief Register announce callback
 * @param cb Callback function
 */
void espnow_ctrl_on_announce(espnow_announce_cb_t cb);

/**
 * @brief Register ACK callback
 * @param cb Callback function
 */
void espnow_ctrl_on_ack(espnow_ack_cb_t cb);

/**
 * @brief Send START command to broadcaster and wait for its ACK (retries).
 * @param msg  Start message to send
 * @return ESP_OK only if the broadcaster acknowledged; ESP_ERR_TIMEOUT otherwise
 */
esp_err_t espnow_ctrl_send_start(const espnow_start_t *msg);

/**
 * @brief Wait for any ACK from the broadcaster.
 * @param ack_cmd    Command expected (currently any ACK counts)
 * @param timeout_ms Maximum wait in milliseconds
 * @return true if an ACK arrived in time
 */
bool espnow_ctrl_wait_ack(uint8_t ack_cmd, uint32_t timeout_ms);

/**
 * @brief Send STOP command to broadcaster
 * @param msg  Stop message to send
 * @return ESP_OK on success
 */
esp_err_t espnow_ctrl_send_stop(const espnow_stop_t *msg);

/**
 * @brief Send CONFIG to broadcaster
 * @param msg  Config message to send
 * @return ESP_OK on success
 */
esp_err_t espnow_ctrl_send_config(const espnow_config_t *msg);

/**
 * @brief Check if broadcaster is paired (seen announce recently)
 * @return true if broadcaster is known
 */
bool espnow_ctrl_is_paired(void);

/**
 * @brief Hold pairing state during an active session.
 *
 * While a session runs the broadcaster stops announcing, so the normal
 * announce-staleness timeout would wrongly report "unpaired". Call with
 * true when a session starts and false when it stops.
 *
 * @param hold  true to keep is_paired() true regardless of announce age
 */
void espnow_ctrl_session_hold(bool hold);

/**
 * @brief Get the paired broadcaster MAC address
 * @param mac  Output buffer (6 bytes)
 * @return true if paired, false otherwise
 */
bool espnow_ctrl_get_broadcaster_mac(uint8_t *mac);

#ifdef __cplusplus
}
#endif
