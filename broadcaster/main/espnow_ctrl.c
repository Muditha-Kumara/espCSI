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

static const uint8_t s_broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

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

            // Remember receiver MAC for sending ACKs, and register it as an
            // ESP-NOW peer so esp_now_send() can reach it.
            memcpy(s_receiver_mac, info->src_addr, 6);
            s_receiver_known = true;
            if (!esp_now_is_peer_exist(s_receiver_mac)) {
                esp_now_peer_info_t peer = {
                    .channel = 0,
                    .ifidx = WIFI_IF_STA,
                    .encrypt = false,
                };
                memcpy(peer.peer_addr, s_receiver_mac, 6);
                esp_err_t perr = esp_now_add_peer(&peer);
                if (perr != ESP_OK && perr != ESP_ERR_ESPNOW_EXIST) {
                    ESP_LOGE(TAG, "Failed to add receiver peer: %s", esp_err_to_name(perr));
                }
            }

            ESP_LOGI(TAG, "Received START: ch=%d rate=%dHz dur=%ds",
                     msg->channel, msg->rate_hz, msg->duration_s);

            // ACK immediately while still on the shared channel, before the
            // handler switches the radio to the work channel.
            espnow_ctrl_send_ack(s_receiver_mac, ESPNOW_CMD_START, 0);

            if (s_start_cb) {
                s_start_cb(msg);
            }
            break;
        }

        case ESPNOW_CMD_STOP: {
            if (len < sizeof(espnow_stop_t)) break;
            const espnow_stop_t *msg = (const espnow_stop_t *)data;

            ESP_LOGI(TAG, "Received STOP");

            if (s_receiver_known) {
                espnow_ctrl_send_ack(s_receiver_mac, ESPNOW_CMD_STOP, 0);
            }

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
        esp_err_t send_ret = esp_now_send(s_broadcast_addr, (const uint8_t *)&msg, sizeof(msg));
        if (send_ret != ESP_OK) {
            ESP_LOGW(TAG, "Announce send failed: %s", esp_err_to_name(send_ret));
        }

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

    // ESP-NOW requires the destination to be registered as a peer before
    // esp_now_send(). Register the broadcast address so announce messages
    // (sent to ff:ff:ff:ff:ff:ff) are actually transmitted.
    if (!esp_now_is_peer_exist(s_broadcast_addr)) {
        esp_now_peer_info_t bcast_peer = {
            .peer_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
            .channel = 0,        // 0 = use current channel
            .ifidx = WIFI_IF_STA,
            .encrypt = false,
        };
        ret = esp_now_add_peer(&bcast_peer);
        if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
            ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Broadcast peer added");
    }

    // Force the broadcast peer's TX PHY rate to OFDM (HT20, MCS0).
    //
    // This is REQUIRED for CSI capture. ESP-NOW data/action frames default to
    // the 1 Mbps 802.11b rate (CCK/DSSS), whose preamble has only a *short*
    // training field. The CSI hardware derives channel estimates from the OFDM
    // long-training field (L-LTF / HT-LTF), so 11b frames yield no CSI at all
    // — which is exactly the "beacons sent, 0 CSI frames" symptom we hit. The
    // official esp-csi csi_send example solves this the same way with
    // esp_now_set_peer_rate_config(). MCS0 LGI keeps maximum range/reliability
    // while still being a proper OFDM frame.
    {
        esp_now_rate_config_t rate_config = {
            .phymode = WIFI_PHY_MODE_HT20,
            .rate    = WIFI_PHY_RATE_MCS0_LGI,
            .ersu    = false,
            .dcm     = false,
        };
        ret = esp_now_set_peer_rate_config(s_broadcast_addr, &rate_config);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set broadcast peer rate: %s", esp_err_to_name(ret));
        }
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
