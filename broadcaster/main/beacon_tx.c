/*
 * Beacon TX - Broadcaster
 * Sends ESP-NOW broadcast frames at a configurable rate
 */

#include "beacon_tx.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "beacon_tx";

#define BEACON_PAYLOAD_SIZE  64
#define BEACON_TASK_STACK    4096

static bool s_active = false;
static uint32_t s_beacon_count = 0;
static TaskHandle_t s_tx_task_handle = NULL;

// Broadcast address (all FF)
static const uint8_t s_broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct {
    uint16_t rate_hz;
    uint32_t duration_s;
} tx_params_t;

static tx_params_t s_params;

static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    // Don't log every send to avoid overhead
    (void)tx_info;
    (void)status;
}

static void beacon_task(void *arg)
{
    tx_params_t *params = (tx_params_t *)arg;
    uint32_t interval_us = 1000000 / params->rate_hz;
    int64_t start_time = esp_timer_get_time();
    int64_t end_time = (params->duration_s > 0) ?
                       (start_time + (int64_t)params->duration_s * 1000000) : 0;

    uint8_t payload[BEACON_PAYLOAD_SIZE];
    memset(payload, 0xAA, sizeof(payload));  // Constant pattern for CSI

    ESP_LOGI(TAG, "Beacon TX started: %d Hz, duration %ds",
             params->rate_hz, params->duration_s);

    while (s_active) {
        // Check duration
        if (end_time > 0 && esp_timer_get_time() >= end_time) {
            ESP_LOGI(TAG, "Beacon duration reached");
            break;
        }

        // Update sequence number in payload
        payload[0] = (s_beacon_count >> 0) & 0xFF;
        payload[1] = (s_beacon_count >> 8) & 0xFF;
        payload[2] = (s_beacon_count >> 16) & 0xFF;
        payload[3] = (s_beacon_count >> 24) & 0xFF;

        // Add timestamp
        int64_t now = esp_timer_get_time();
        memcpy(&payload[4], &now, sizeof(now));

        esp_err_t ret = esp_now_send(s_broadcast_addr, payload, sizeof(payload));
        if (ret == ESP_OK) {
            s_beacon_count++;
        }

        // Sleep for the interval
        vTaskDelay(pdMS_TO_TICKS(interval_us / 1000));
    }

    s_active = false;
    ESP_LOGI(TAG, "Beacon TX stopped after %u beacons", s_beacon_count);
    s_tx_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t beacon_tx_init(void)
{
    esp_now_register_send_cb(espnow_send_cb);
    ESP_LOGI(TAG, "Beacon TX initialized");
    return ESP_OK;
}

esp_err_t beacon_tx_start(uint16_t rate_hz, uint32_t duration_s)
{
    if (s_active) {
        ESP_LOGW(TAG, "Beacon TX already active");
        return ESP_ERR_INVALID_STATE;
    }

    if (rate_hz < 10 || rate_hz > 200) {
        ESP_LOGE(TAG, "Invalid rate: %d Hz (must be 10-200)", rate_hz);
        return ESP_ERR_INVALID_ARG;
    }

    s_params.rate_hz = rate_hz;
    s_params.duration_s = duration_s;
    s_beacon_count = 0;
    s_active = true;

    BaseType_t ret = xTaskCreate(beacon_task, "beacon_tx", BEACON_TASK_STACK,
                                  &s_params, tskIDLE_PRIORITY + 2, &s_tx_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create beacon task");
        s_active = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void beacon_tx_stop(void)
{
    s_active = false;
    // Task will exit on its own
}

bool beacon_tx_is_active(void)
{
    return s_active;
}

uint32_t beacon_tx_get_count(void)
{
    return s_beacon_count;
}
