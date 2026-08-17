/*
 * ESP-NOW Control - Broadcaster
 * Announces presence, receives commands from receiver
 */

#include "espnow_ctrl.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "espnow_ctrl";

#define ANNOUNCE_INTERVAL_MS  5000
#define ANNOUNCE_TASK_STACK   4096

static espnow_start_cb_t s_start_cb = NULL;
static espnow_stop_cb_t s_stop_cb = NULL;
static espnow_config_cb_t s_config_cb = NULL;
static TaskHandle_t s_announce_task_handle = NULL;
static bool s_announcing = false;
static uint8_t s_receiver_mac[6] = {0};
static bool s_receiver_known = false;

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (info == NULL || data == NULL || len < sizeof(uint32_t)) return;

    uint32_t magic;
    memcpy(&magic, data, sizeof(magic));
    if (magic != ESPNOW_PROTO_MAGIC) return;

    uint8_t cmd = data[4];

    switch (cmd) {
        case ESPNOW_CMD_START: {
            if (len < sizeof(espnow_start_t)) break;
            const espnow_start_t *msg = (const espnow_start_t *)data;

            // Remember receiver MAC for sending ACKs
            memcpy(s_receiver_mac, info->src_addr, 6);
            s_receiver_known = true;

            ESP_LOGI(TAG, "Received START: ch=%d rate=%dHz dur=%ds",
                     msg->channel, msg->rate_hz, msg->duration_s);

            if (s_start_cb) {
                s_start_cb(msg);
            }
            break;
        }

        case ESPNOW_CMD_STOP: {
            if (len < sizeof(espnow_stop_t)) break;
            const espnow_stop_t *msg = (const espnow_stop_t *)data;

            ESP_LOGI(TAG, "Received STOP");

            if (s_stop_cb) {
                s_stop_cb(msg);
            }
            break;
        }

        case ESPNOW_CMD_CONFIG: {
            if (len < sizeof(espnow_config_t)) break;
            const espnow_config_t *msg = (const espnow_config_t *)data;

            ESP_LOGI(TAG, "Received CONFIG: ota_url=%s", msg->ota_url);

            if (s_config_cb) {
                s_config_cb(msg);
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown command: %d", cmd);
            break;
    }
}

static void announce_task(void *arg)
{
    const esp_app_desc_t *app = esp_app_get_description();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    while (s_announcing) {
        espnow_announce_t msg = {
            .magic = ESPNOW_PROTO_MAGIC,
            .cmd = ESPNOW_CMD_ANNOUNCE,
            .channel = 0,
        };
        memcpy(msg.mac, mac, 6);
        strncpy(msg.version, app->version, sizeof(msg.version) - 1);

        // Get current channel
        uint8_t primary;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&primary, &second);
        msg.channel = primary;

        // Broadcast announce
        uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_send(broadcast, (const uint8_t *)&msg, sizeof(msg));

        vTaskDelay(pdMS_TO_TICKS(ANNOUNCE_INTERVAL_MS));
    }

    s_announce_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t espnow_ctrl_init(void)
{
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_now_register_recv_cb(espnow_recv_cb);

    ESP_LOGI(TAG, "ESP-NOW control initialized");
    return ESP_OK;
}

void espnow_ctrl_on_start(espnow_start_cb_t cb)
{
    s_start_cb = cb;
}

void espnow_ctrl_on_stop(espnow_stop_cb_t cb)
{
    s_stop_cb = cb;
}

void espnow_ctrl_on_config(espnow_config_cb_t cb)
{
    s_config_cb = cb;
}

esp_err_t espnow_ctrl_start_announce(void)
{
    if (s_announcing) return ESP_ERR_INVALID_STATE;

    s_announcing = true;
    BaseType_t ret = xTaskCreate(announce_task, "announce", ANNOUNCE_TASK_STACK,
                                  NULL, tskIDLE_PRIORITY + 1, &s_announce_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create announce task");
        s_announcing = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Announce task started (every %ds)", ANNOUNCE_INTERVAL_MS / 1000);
    return ESP_OK;
}

void espnow_ctrl_stop_announce(void)
{
    s_announcing = false;
}

esp_err_t espnow_ctrl_send_ack(const uint8_t *dest_mac, uint8_t ack_cmd, uint8_t status)
{
    espnow_ack_t msg = {
        .magic = ESPNOW_PROTO_MAGIC,
        .cmd = ESPNOW_CMD_ACK,
        .ack_cmd = ack_cmd,
        .status = status,
    };

    return esp_now_send(dest_mac, (const uint8_t *)&msg, sizeof(msg));
}
