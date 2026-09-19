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
static uint32_t s_raw_count = 0;
static uint32_t s_filtered_count = 0;
static csi_data_cb_t s_callback = NULL;
static char s_csv_buf[2048];
static uint8_t s_source_mac[6];
static bool s_filter_source = false;

void csi_capture_set_source_mac(const uint8_t *mac)
{
    if (mac) {
        memcpy(s_source_mac, mac, 6);
        s_filter_source = true;
    } else {
        s_filter_source = false;
    }
}

static void csi_rx_callback(void *ctx, wifi_csi_info_t *info)
{
    (void)ctx;
    if (!s_active || info == NULL || s_callback == NULL) return;

    // Accept CSI from all modulation types: ESP-NOW beacon frames are sent
    // as legacy (11b/g) frames, so filtering on sig_mode would drop them.
    if (info->buf == NULL || info->len == 0) return;

    // Diagnostic: count every raw CSI frame that reaches the callback, before
    // the source filter. The first few are logged at debug level so a verbose
    // build can confirm the broadcaster's frames arrive and under which MAC.
    s_raw_count++;
    if (s_raw_count <= 5) {
        ESP_LOGD(TAG, "CSI raw #%lu mac=" MACSTR " ch=%d rssi=%d len=%d",
                 (unsigned long)s_raw_count, MAC2STR(info->mac),
                 info->rx_ctrl.channel, info->rx_ctrl.rssi, info->len);
    }

    // Optional source-MAC filter: only keep frames from the broadcaster.
    if (s_filter_source && memcmp(info->mac, s_source_mac, 6) != 0) {
        s_filtered_count++;
        return;
    }

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
    s_raw_count = 0;
    s_filtered_count = 0;

    // The receiver disconnects from the AP for the session (so it can sit on
    // an arbitrary channel). A disconnected station normally receives no
    // frames, so the CSI callback would never fire. Enabling promiscuous
    // (sniffer) mode makes the radio pass every frame on the channel up for
    // CSI extraction regardless of association — this is the approach the
    // official Espressif esp-csi examples use.
    //
    // IMPORTANT (verified on ESP-IDF v6 / ESP32-S3): do NOT register a
    // promiscuous RX callback. The esp-csi reference examples call
    // esp_wifi_set_promiscuous(true) with no cb — the CSI side-channel then
    // receives the frames. Registering a dummy cb instead makes the driver
    // route RX buffers to the sniffer callback and the CSI callback never
    // fires (0 frames despite the radio clearly receiving).
    //
    // Also enable promiscuous *before* esp_wifi_set_csi_config(): the IDF v6
    // docs state it fails unless "WiFi is started or promiscuous mode is
    // enabled".
    esp_wifi_set_promiscuous_rx_cb(NULL);
    // The default promiscuous filter passes only WIFI_PKT_MISC (zero-payload
    // MIMO notifications) and drops DATA frames — which is what ESP-NOW
    // beacons are. Without DATA in the filter the CSI side-channel never sees
    // the broadcaster's frames. Explicitly pass every packet type instead.
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
                     | WIFI_PROMIS_FILTER_MASK_CTRL
                     | WIFI_PROMIS_FILTER_MASK_DATA
                     | WIFI_PROMIS_FILTER_MASK_MISC,
    };
    esp_err_t ret = esp_wifi_set_promiscuous_filter(&filter);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set promiscuous filter: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_wifi_set_promiscuous(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable promiscuous mode: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure CSI — matches the official esp-csi csi_recv defaults for the
    // ESP32-S3 legacy config. Note channel_filter_en is a subcarrier-smoothing
    // filter (it smooths adjacent sub-carriers), NOT a per-channel frame
    // filter, so it does not drop frames on a retuned channel. The real reason
    // CSI was empty is on the sender: ESP-NOW defaults to 1 Mbps 802.11b
    // (CCK/DSSS) frames that carry no OFDM long-training field, so the CSI
    // hardware has nothing to extract. The broadcaster now forces an OFDM
    // (HT20 MCS0) peer rate — see broadcaster/main/espnow_ctrl.c.
    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
        .shift = false,
    };
    ret = esp_wifi_set_csi_config(&csi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set CSI config: %s", esp_err_to_name(ret));
        esp_wifi_set_promiscuous(false);
        return ret;
    }

    ret = esp_wifi_set_csi_rx_cb(csi_rx_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set CSI callback: %s", esp_err_to_name(ret));
        esp_wifi_set_promiscuous(false);
        return ret;
    }

    ret = esp_wifi_set_csi(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable CSI: %s", esp_err_to_name(ret));
        esp_wifi_set_promiscuous(false);
        return ret;
    }

    s_active = true;
    ESP_LOGI(TAG, "CSI capture started on channel %d (promiscuous)", channel);
    return ESP_OK;
}

void csi_capture_stop(void)
{
    if (!s_active) return;

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_csi(false);
    s_active = false;
    s_callback = NULL;
    ESP_LOGI(TAG, "CSI capture stopped (%u frames, %u raw, %u filtered-out)",
             s_frame_count, s_raw_count, s_filtered_count);
}

bool csi_capture_is_active(void)
{
    return s_active;
}

uint32_t csi_capture_get_count(void)
{
    return s_frame_count;
}
