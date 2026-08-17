/*
 * Broadcaster App Main - ESP-WROOM-32 ESP-NOW Beacon Transmitter
 * Main entry point for the broadcaster firmware
 */

#include "led.h"
#include "wifi_sta.h"
#include "beacon_tx.h"
#include "espnow_ctrl.h"
#include "ota_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "app_main";

// Current session state
static bool s_session_active = false;
static uint8_t s_work_channel = 0;
static uint16_t s_work_rate = 0;
static uint32_t s_work_duration = 0;

// START command handler
static void on_start_command(const espnow_start_t *msg)
{
    ESP_LOGI(TAG, "Starting session: ch=%d rate=%dHz dur=%ds",
             msg->channel, msg->rate_hz, msg->duration_s);

    // Stop announce task during session
    espnow_ctrl_stop_announce();

    // Switch to work channel
    wifi_sta_set_channel(msg->channel);
    s_work_channel = msg->channel;
    s_work_rate = msg->rate_hz;
    s_work_duration = msg->duration_s;

    // Start beacon transmission
    esp_err_t ret = beacon_tx_start(msg->rate_hz, msg->duration_s);
    if (ret == ESP_OK) {
        s_session_active = true;
        led_blink(100, 100);  // Fast blink during session
    } else {
        ESP_LOGE(TAG, "Failed to start beacon TX");
        wifi_sta_restore_channel();
        espnow_ctrl_start_announce();
    }
}

// STOP command handler
static void on_stop_command(const espnow_stop_t *msg)
{
    if (!s_session_active) {
        ESP_LOGW(TAG, "No session active");
        return;
    }

    ESP_LOGI(TAG, "Stopping session");

    beacon_tx_stop();
    s_session_active = false;

    // Restore Wi-Fi channel
    wifi_sta_restore_channel();

    // Resume announce
    espnow_ctrl_start_announce();

    led_on();  // Solid on when idle
}

// CONFIG command handler
static void on_config_command(const espnow_config_t *msg)
{
    ESP_LOGI(TAG, "Received config: %s", msg->ota_url);
    // Config is informational, no action needed for now
}

// Session monitor task - auto-stop when duration expires
static void session_monitor_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (s_session_active && !beacon_tx_is_active()) {
            // Beacon task finished (duration expired)
            ESP_LOGI(TAG, "Session duration reached, stopping...");
            on_stop_command(NULL);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-CSI Broadcaster starting...");

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

    // Initialize beacon TX
    ESP_ERROR_CHECK(beacon_tx_init());

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(espnow_ctrl_init());
    espnow_ctrl_on_start(on_start_command);
    espnow_ctrl_on_stop(on_stop_command);
    espnow_ctrl_on_config(on_config_command);

    // Start announce task
    ESP_ERROR_CHECK(espnow_ctrl_start_announce());

    // Initialize OTA client
    ESP_ERROR_CHECK(ota_client_init());
    ESP_ERROR_CHECK(ota_client_start_periodic());

    // Start session monitor
    xTaskCreate(session_monitor_task, "session_mon", 4096, NULL, tskIDLE_PRIORITY, NULL);

    ESP_LOGI(TAG, "Broadcaster ready! Announcing every 5s...");

    // Main loop - just keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
