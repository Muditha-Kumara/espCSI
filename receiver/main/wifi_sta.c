/*
 * Wi-Fi Station Mode - Receiver
 * Connects to home Wi-Fi network, manages channel switching for CSI
 */

#include "wifi_sta.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "wifi_sta";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      10

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_sta_netif = NULL;
static int s_retry_count = 0;
static bool s_connected = false;
static uint8_t s_home_channel = 0;
static volatile bool s_suppress_reconnect = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_suppress_reconnect) {
            // Intentional disconnect (CSI session on another channel):
            // do NOT reconnect, otherwise the STA is dragged back to the
            // home AP channel and the session breaks.
            ESP_LOGI(TAG, "Disconnected (reconnect suppressed for session)");
        } else if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying connection (%d/%d)", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGW(TAG, "Connection failed after %d retries", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_connected = true;

        // Read current channel
        uint8_t primary;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&primary, &second);
        s_home_channel = primary;
        ESP_LOGI(TAG, "Home AP channel: %d", s_home_channel);

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_sta_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    esp_event_handler_instance_t handler_instance;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &handler_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &handler_instance));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Disable power save: modem sleep makes the STA miss ESP-NOW broadcast
    // frames (announces) between DTIM windows.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Allow channels 1-13: with the default AUTO policy the STA inherits the
    // home AP's country info, which often limits 2.4GHz to channels 1-11 and
    // makes esp_wifi_set_channel(13) fail.
    wifi_country_t country = {
        .cc = "CN",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    ESP_LOGI(TAG, "WiFi STA initialized, connecting to '%s'", CONFIG_WIFI_SSID);
    return ESP_OK;
}

esp_err_t wifi_sta_wait_connect(int timeout_ms)
{
    if (s_wifi_event_group == NULL) return ESP_ERR_INVALID_STATE;

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE,
            pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to '%s'", CONFIG_WIFI_SSID);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to connect");
    return ESP_ERR_TIMEOUT;
}

bool wifi_sta_is_connected(void)
{
    return s_connected;
}

void receiver_wifi_sta_disconnect(void)
{
    esp_wifi_disconnect();
    s_connected = false;
}

void receiver_wifi_sta_reconnect(void)
{
    s_retry_count = 0;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_wifi_connect();
}

uint8_t wifi_sta_get_channel(void)
{
    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    return primary;
}

esp_err_t wifi_sta_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 13) return ESP_ERR_INVALID_ARG;

    // esp_wifi_set_channel() is rejected while the STA is associated or
    // mid-connect ("STA is scanning or connecting ... cannot set channel").
    // The session runs off the home AP anyway, so disconnect first.
    esp_wifi_disconnect();
    s_connected = false;
    vTaskDelay(pdMS_TO_TICKS(150));

    // Re-assert the country after disconnecting: the driver may revert to
    // the AP's (AUTO) country info on disconnect, which would clamp ch 12-13.
    wifi_country_t country = {
        .cc = "CN",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    esp_wifi_set_country(&country);

    uint8_t primary = 0;
    wifi_second_chan_t second = 0;
    for (int i = 0; i < 20; i++) {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_get_channel(&primary, &second);
        if (primary == channel) {
            ESP_LOGI(TAG, "Radio tuned to channel %d", primary);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGE(TAG, "Failed to tune to channel %d (radio at %d)", channel, primary);
    return ESP_FAIL;
}

void wifi_sta_set_reconnect_suppressed(bool suppress)
{
    s_suppress_reconnect = suppress;
    if (!suppress) {
        s_retry_count = 0;
    }
}

bool wifi_sta_is_reconnect_suppressed(void)
{
    return s_suppress_reconnect;
}

esp_err_t wifi_sta_restore_channel(void)
{
    if (s_home_channel == 0) return ESP_ERR_INVALID_STATE;

    // End of session: allow reconnects again, move back to the home channel
    // and re-associate explicitly (the channel switch disconnected the STA
    // while reconnects were suppressed).
    s_suppress_reconnect = false;
    s_retry_count = 0;
    esp_err_t ret = esp_wifi_set_channel(s_home_channel, WIFI_SECOND_CHAN_NONE);
    if (!s_connected) {
        esp_wifi_connect();
    }
    return ret;
}
