/*
 * CSI Capture - Receiver
 * Registers CSI callback, formats data as CSV lines
 */

#include "csi_capture.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "csi_capture";

static bool s_active = false;
static uint32_t s_frame_count = 0;
static csi_data_cb_t s_callback = NULL;
static char s_csv_buf[2048];

static void csi_rx_callback(void *ctx, wifi_csi_info_t *info)
{
    if (!s_active || info == NULL || s_callback == NULL) return;

    // Only process LLTF data from HT (11n) frames for consistency
    if (info->rx_ctrl.sig_mode != 1) return;  // 1 = 11n HT
    if (info->buf == NULL || info->len == 0) return;

    s_frame_count++;

    // Format: CSI_DATA,<id>,<mac>,<rssi>,<rate>,<noise_floor>,<channel>,<timestamp>,<sig_len>,<rx_format>,<len>,<first_word_invalid>,"[i,q,...]"
    int offset = snprintf(s_csv_buf, sizeof(s_csv_buf),
        "CSI_DATA,%lu," MACSTR ",%d,%d,%d,%d,%lld,%d,%d,%d,%d,\"[",
        (unsigned long)s_frame_count,
        MAC2STR(info->mac),
        info->rx_ctrl.rssi,
        info->rx_ctrl.rate,
        info->rx_ctrl.noise_floor,
        info->rx_ctrl.channel,
        (long long)esp_timer_get_time(),
        info->rx_ctrl.sig_len,
        info->rx_ctrl.rx_state,
        info->len,
        info->first_word_invalid);

    // Append I/Q pairs
    const int8_t *data = info->buf;
    int data_len = info->len;
    bool first = true;
    for (int i = 0; i < data_len - 1; i += 2) {
        if (!first) {
            offset += snprintf(s_csv_buf + offset, sizeof(s_csv_buf) - offset, ",");
        }
        offset += snprintf(s_csv_buf + offset, sizeof(s_csv_buf) - offset,
                          "%d,%d", data[i], data[i + 1]);
        first = false;
    }

    offset += snprintf(s_csv_buf + offset, sizeof(s_csv_buf) - offset, "]\"\n");

    s_callback(s_csv_buf, offset);
}

esp_err_t csi_capture_init(void)
{
    ESP_LOGI(TAG, "CSI capture initialized");
    return ESP_OK;
}

esp_err_t csi_capture_start(uint8_t channel, csi_data_cb_t cb)
{
    if (s_active) {
        ESP_LOGW(TAG, "CSI capture already active");
        return ESP_ERR_INVALID_STATE;
    }

    s_callback = cb;
    s_frame_count = 0;

    // Configure CSI
    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
        .shift = false,
    };
    esp_err_t ret = esp_wifi_set_csi_config(&csi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set CSI config: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_csi_rx_cb(csi_rx_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set CSI callback: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_csi(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable CSI: %s", esp_err_to_name(ret));
        return ret;
    }

    s_active = true;
    ESP_LOGI(TAG, "CSI capture started on channel %d", channel);
    return ESP_OK;
}

void csi_capture_stop(void)
{
    if (!s_active) return;

    esp_wifi_set_csi(false);
    s_active = false;
    s_callback = NULL;
    ESP_LOGI(TAG, "CSI capture stopped (%u frames)", s_frame_count);
}

bool csi_capture_is_active(void)
{
    return s_active;
}

uint32_t csi_capture_get_count(void)
{
    return s_frame_count;
}
