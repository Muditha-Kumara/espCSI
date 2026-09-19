/*
 * Receiver App Main - ESP32-S3 CSI Data Collector
 * Main entry point for the receiver firmware
 */

#include "led.h"
#include "wifi_sta.h"
#include "csi_capture.h"
#include "session_store.h"
#include "espnow_ctrl.h"
#include "ota_client.h"
#include "web_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "app_main";

// Session auto-stop task
static void session_monitor_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (web_server_session_is_running()) {
            uint32_t elapsed = web_server_session_elapsed_s();
            uint32_t duration = web_server_session_duration_s();
            // Grace period of 5s beyond the configured duration: the
            // broadcaster stops beaming on its own timer; we then close the
            // session, restore the channel and reconnect.
            if (duration > 0 && elapsed > duration + 5) {
                ESP_LOGI(TAG, "Session duration reached, auto-stopping");
                web_server_stop_session();
            }
        }
    }
}

// Broadcaster announce callback
static void on_broadcaster_announce(const uint8_t *mac, uint8_t channel, const char *version)
{
    ESP_LOGI(TAG, "Broadcaster detected: " MACSTR " ch=%d ver=%s",
             MAC2STR(mac), channel, version);
}

// ACK callback
static void on_broadcaster_ack(uint8_t ack_cmd, uint8_t status)
{
    ESP_LOGI(TAG, "Broadcaster ACK: cmd=%d status=%d", ack_cmd, status);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-CSI Receiver starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize LED
    ESP_ERROR_CHECK(led_init());
    led_blink(500, 500);  // Slow blink during init

    // Initialize Wi-Fi
    ESP_ERROR_CHECK(wifi_sta_init());
    ESP_ERROR_CHECK(wifi_sta_wait_connect(30000));

    if (!wifi_sta_is_connected()) {
        ESP_LOGE(TAG, "WiFi connection failed, restarting...");
        led_blink(100, 100);  // Fast blink on error
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    led_on();  // Solid on when connected

    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP-CSI Receiver"));
    ESP_LOGI(TAG, "mDNS: http://%s.local", CONFIG_MDNS_HOSTNAME);

    // Initialize session store (FATFS)
    ESP_ERROR_CHECK(session_store_init());

    // Initialize CSI capture
    ESP_ERROR_CHECK(csi_capture_init());

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(espnow_ctrl_init());
    espnow_ctrl_on_announce(on_broadcaster_announce);
    espnow_ctrl_on_ack(on_broadcaster_ack);

    // Initialize OTA client
    ESP_ERROR_CHECK(ota_client_init());
    ESP_ERROR_CHECK(ota_client_start_periodic());

    // Start web server
    ESP_ERROR_CHECK(web_server_init());

    // Start session monitor
    xTaskCreate(session_monitor_task, "session_mon", 4096, NULL, tskIDLE_PRIORITY, NULL);

    ESP_LOGI(TAG, "Receiver ready! Browse to http://%s.local", CONFIG_MDNS_HOSTNAME);

    // Main loop - just keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
