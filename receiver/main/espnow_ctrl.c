/*
 * ESP-NOW Control - Receiver
 * Receives broadcaster announcements, sends control commands
 */

#include "espnow_ctrl.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "espnow_ctrl";

static uint8_t s_broadcaster_mac[6] = {0};
static bool s_paired = false;
static int64_t s_last_announce_time = 0;
static espnow_announce_cb_t s_announce_cb = NULL;
static espnow_ack_cb_t s_ack_cb = NULL;
static SemaphoreHandle_t s_send_mutex = NULL;

#define ANNOUNCE_TIMEOUT_MS  15000  // Consider unpaired after 15s without announce

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (info == NULL || data == NULL || len < sizeof(uint32_t)) return;

    // Check magic
    uint32_t magic;
    memcpy(&magic, data, sizeof(magic));
    if (magic != ESPNOW_PROTO_MAGIC) return;

    uint8_t cmd = data[4];

    switch (cmd) {
        case ESPNOW_CMD_ANNOUNCE: {
            if (len < sizeof(espnow_announce_t)) break;
            const espnow_announce_t *msg = (const espnow_announce_t *)data;

            // Update broadcaster info
            memcpy(s_broadcaster_mac, msg->mac, 6);
            s_paired = true;
            s_last_announce_time = esp_timer_get_time() / 1000;

            ESP_LOGD(TAG, "Announce from " MACSTR " ch=%d ver=%s",
                     MAC2STR(msg->mac), msg->channel, msg->version);

            if (s_announce_cb) {
                s_announce_cb(msg->mac, msg->channel, msg->version);
            }
            break;
        }

        case ESPNOW_CMD_ACK: {
            if (len < sizeof(espnow_ack_t)) break;
            const espnow_ack_t *msg = (const espnow_ack_t *)data;

            ESP_LOGD(TAG, "ACK for cmd %d, status %d", msg->ack_cmd, msg->status);

            if (s_ack_cb) {
                s_ack_cb(msg->ack_cmd, msg->status);
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown command: %d", cmd);
            break;
    }
}

esp_err_t espnow_ctrl_init(void)
{
    s_send_mutex = xSemaphoreCreateMutex();
    if (s_send_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_now_register_recv_cb(espnow_recv_cb);

    ESP_LOGI(TAG, "ESP-NOW control initialized");
    return ESP_OK;
}

void espnow_ctrl_on_announce(espnow_announce_cb_t cb)
{
    s_announce_cb = cb;
}

void espnow_ctrl_on_ack(espnow_ack_cb_t cb)
{
    s_ack_cb = cb;
}

esp_err_t espnow_ctrl_send_start(const espnow_start_t *msg)
{
    if (!s_paired) {
        ESP_LOGW(TAG, "No broadcaster paired");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    esp_err_t ret = esp_now_send(s_broadcaster_mac, (const uint8_t *)msg, sizeof(*msg));
    xSemaphoreGive(s_send_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send START: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent START command");
    }
    return ret;
}

esp_err_t espnow_ctrl_send_stop(const espnow_stop_t *msg)
{
    if (!s_paired) {
        ESP_LOGW(TAG, "No broadcaster paired");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    esp_err_t ret = esp_now_send(s_broadcaster_mac, (const uint8_t *)msg, sizeof(*msg));
    xSemaphoreGive(s_send_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send STOP: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent STOP command");
    }
    return ret;
}

esp_err_t espnow_ctrl_send_config(const espnow_config_t *msg)
{
    if (!s_paired) {
        ESP_LOGW(TAG, "No broadcaster paired");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    esp_err_t ret = esp_now_send(s_broadcaster_mac, (const uint8_t *)msg, sizeof(*msg));
    xSemaphoreGive(s_send_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send CONFIG: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent CONFIG command");
    }
    return ret;
}

bool espnow_ctrl_is_paired(void)
{
    if (!s_paired) return false;

    // Check if announce is stale
    int64_t now = esp_timer_get_time() / 1000;
    if ((now - s_last_announce_time) > ANNOUNCE_TIMEOUT_MS) {
        s_paired = false;
        return false;
    }
    return true;
}

bool espnow_ctrl_get_broadcaster_mac(uint8_t *mac)
{
    if (!espnow_ctrl_is_paired()) return false;
    memcpy(mac, s_broadcaster_mac, 6);
    return true;
}
