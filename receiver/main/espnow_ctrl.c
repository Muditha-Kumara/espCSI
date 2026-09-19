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
static bool s_session_hold = false;
static int64_t s_last_announce_time = 0;
static espnow_announce_cb_t s_announce_cb = NULL;
static espnow_ack_cb_t s_ack_cb = NULL;
static SemaphoreHandle_t s_send_mutex = NULL;
// Signaled once per ACK received; used by espnow_ctrl_wait_ack() so that
// control commands can be confirmed (and retried) before we move the radio.
static SemaphoreHandle_t s_ack_sem = NULL;

// MAC-layer confirmation that a START frame we sent was ACKed by the
// broadcaster's radio. esp_now_send() is asynchronous: the send callback
// fires when the peer's 802.11 hardware acknowledges the frame, which
// happens *before* the peer's application can retune its radio. This is the
// reliable handshake — the application-level ESPNOW_CMD_ACK is often lost
// because the broadcaster switches channels milliseconds after receiving
// START, killing the queued ACK frame. s_start_tx_status records the result
// of the most recent START send; s_start_tx_sem signals when it is known.
static SemaphoreHandle_t s_start_tx_sem = NULL;
static esp_now_send_status_t s_start_tx_status = ESP_NOW_SEND_FAIL;

static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    // The send callback is global; latch the result of the most recent
    // esp_now_send() (which, in this module, is only ever START/STOP/CONFIG
    // to the broadcaster) and wake any waiter.
    (void)tx_info;
    s_start_tx_status = status;
    if (s_start_tx_sem) {
        xSemaphoreGive(s_start_tx_sem);
    }
}

#define ANNOUNCE_TIMEOUT_MS  15000  // Consider unpaired after 15s without announce

// Add (or refresh) a unicast ESP-NOW peer. ESP-NOW requires the destination
// to be registered before esp_now_send() can deliver data to it.
static esp_err_t ensure_peer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) return ESP_OK;

    esp_now_peer_info_t peer = {
        .channel = 0,        // 0 = use current channel
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, mac, 6);
    return esp_now_add_peer(&peer);
}

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (info == NULL || data == NULL || len < sizeof(uint32_t)) return;

    // Check magic. Frames that are not our control protocol (e.g. the
    // broadcaster's CSI beacon payloads, which arrive here as ESP-NOW
    // broadcasts) are expected and handled by the CSI path — ignore them.
    uint32_t magic;
    memcpy(&magic, data, sizeof(magic));
    if (magic != ESPNOW_PROTO_MAGIC) {
        return;
    }

    uint8_t cmd = data[4];

    switch (cmd) {
        case ESPNOW_CMD_ANNOUNCE: {
            if (len < sizeof(espnow_announce_t)) break;
            const espnow_announce_t *msg = (const espnow_announce_t *)data;

            // Update broadcaster info
            bool newly_paired = !s_paired || memcmp(s_broadcaster_mac, msg->mac, 6) != 0;
            if (newly_paired && s_paired) {
                // Broadcaster MAC changed: drop the stale peer entry
                esp_now_del_peer(s_broadcaster_mac);
            }
            memcpy(s_broadcaster_mac, msg->mac, 6);
            s_paired = true;
            s_last_announce_time = esp_timer_get_time() / 1000;

            // Register the broadcaster as an ESP-NOW peer so that START/STOP
            // commands can be sent to it (required by esp_now_send).
            if (newly_paired) {
                esp_err_t perr = ensure_peer(s_broadcaster_mac);
                if (perr != ESP_OK && perr != ESP_ERR_ESPNOW_EXIST) {
                    ESP_LOGE(TAG, "Failed to add broadcaster peer: %s", esp_err_to_name(perr));
                } else {
                    ESP_LOGI(TAG, "Paired with broadcaster " MACSTR, MAC2STR(msg->mac));
                }
            }

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

            if (s_ack_sem) {
                xSemaphoreGive(s_ack_sem);
            }
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

    s_ack_sem = xSemaphoreCreateCounting(8, 0);
    if (s_ack_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create ACK semaphore");
        return ESP_ERR_NO_MEM;
    }

    s_start_tx_sem = xSemaphoreCreateBinary();
    if (s_start_tx_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create START tx semaphore");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_now_register_recv_cb(espnow_recv_cb);
    esp_now_register_send_cb(espnow_send_cb);

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

    // Confirm delivery via the 802.11 MAC-layer send callback: the
    // broadcaster's radio hardware ACKs the START frame the instant it
    // receives it, while still on the shared home channel. This is strictly
    // more reliable than the application-level ESPNOW_CMD_ACK, which is
    // frequently lost because the broadcaster retunes its radio milliseconds
    // after processing START, before the queued ACK frame goes out.
    const int max_attempts = 4;
    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        // Drain any stale tx result, then send and wait for the callback.
        while (xSemaphoreTake(s_start_tx_sem, 0) == pdTRUE) {}

        xSemaphoreTake(s_send_mutex, portMAX_DELAY);
        esp_err_t ret = esp_now_send(s_broadcaster_mac, (const uint8_t *)msg, sizeof(*msg));
        xSemaphoreGive(s_send_mutex);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send START: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        ESP_LOGI(TAG, "Sent START command (attempt %d)", attempt);

        if (xSemaphoreTake(s_start_tx_sem, pdMS_TO_TICKS(600)) == pdTRUE) {
            if (s_start_tx_status == ESP_NOW_SEND_SUCCESS) {
                ESP_LOGI(TAG, "START delivered (MAC-layer ACK)");
                return ESP_OK;
            }
            ESP_LOGW(TAG, "START send failed at MAC layer (attempt %d)", attempt);
        } else {
            ESP_LOGW(TAG, "No MAC-layer result for START after 600 ms (attempt %d)", attempt);
        }
    }
    return ESP_ERR_TIMEOUT;
}

bool espnow_ctrl_wait_ack(uint8_t ack_cmd, uint32_t timeout_ms)
{
    (void)ack_cmd;  // ACKs are rare enough that counting them suffices.
    if (!s_ack_sem) return false;
    return xSemaphoreTake(s_ack_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
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

    // During an active session the broadcaster stops announcing (it is busy
    // beaming CSI frames), so the announce-staleness check does not apply.
    if (s_session_hold) return true;

    // Check if announce is stale
    int64_t now = esp_timer_get_time() / 1000;
    if ((now - s_last_announce_time) > ANNOUNCE_TIMEOUT_MS) {
        s_paired = false;
        return false;
    }
    return true;
}

void espnow_ctrl_session_hold(bool hold)
{
    s_session_hold = hold;
}

bool espnow_ctrl_get_broadcaster_mac(uint8_t *mac)
{
    if (!espnow_ctrl_is_paired()) return false;
    memcpy(mac, s_broadcaster_mac, 6);
    return true;
}
