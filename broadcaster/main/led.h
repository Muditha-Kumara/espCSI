/*
 * LED Status Indicator - Broadcaster
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t led_init(void);
void led_on(void);
void led_off(void);
void led_toggle(void);
void led_blink(int on_ms, int off_ms);
void led_stop_blink(void);

#ifdef __cplusplus
}
#endif
