/*
 * LED Status Indicator - Receiver
 * Controls status LED for visual feedback
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the status LED GPIO
 * @return ESP_OK on success
 */
esp_err_t led_init(void);

/**
 * @brief Turn LED on
 */
void led_on(void);

/**
 * @brief Turn LED off
 */
void led_off(void);

/**
 * @brief Toggle LED state
 */
void led_toggle(void);

/**
 * @brief Start LED blinking pattern
 * @param on_ms  ON duration in milliseconds
 * @param off_ms OFF duration in milliseconds
 */
void led_blink(int on_ms, int off_ms);

/**
 * @brief Stop blinking, turn LED off
 */
void led_stop_blink(void);

#ifdef __cplusplus
}
#endif
