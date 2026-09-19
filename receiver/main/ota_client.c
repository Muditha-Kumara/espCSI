/*
 * OTA Client - Receiver
 * Periodic and manual OTA firmware updates via HTTPS
 */

#include "ota_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ota_client";

#define OTA_CHECK_INTERVAL_MS  (30 * 60 * 1000)  // 30 minutes
#define OTA_TASK_STACK_SIZE    8192
#define OTA_URL_MAX_LEN        128

static ota_state_t s_state = OTA_STATE_IDLE;
static char s_status_msg[128] = "Idle";
static SemaphoreHandle_t s_ota_mutex = NULL;
static SemaphoreHandle_t s_wake_sem = NULL;
static TaskHandle_t s_ota_task_handle = NULL;
static bool s_periodic_running = false;

static char s_ota_url[OTA_URL_MAX_LEN] = {0};

static void build_ota_url(void)
{
    snprintf(s_ota_url, sizeof(s_ota_url), "%s/firmware/receiver", CONFIG_OTA_SERVER_URL);
}

static esp_err_t check_version(void)
{
    char url[OTA_URL_MAX_LEN];
    snprintf(url, sizeof(url), "%s/version/receiver", CONFIG_OTA_SERVER_URL);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_http_client_perform(client);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Version check failed: %s", esp_err_to_name(ret));
        esp_http_client_cleanup(client);
        return ret;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "Version check returned %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char server_version[32] = {0};
    int len = esp_http_client_read(client, server_version, sizeof(server_version) - 1);
    esp_http_client_cleanup(client);

    if (len <= 0) {
        ESP_LOGW(TAG, "Empty version response");
        return ESP_FAIL;
    }
    server_version[len] = '\0';

    // Trim whitespace
    while (len > 0 && (server_version[len-1] == '\n' || server_version[len-1] == '\r')) {
        server_version[--len] = '\0';
    }

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Current: %s, Server: %s", app->version, server_version);

    if (strcmp(app->version, server_version) == 0) {
        snprintf(s_status_msg, sizeof(s_status_msg), "Up to date (%s)", app->version);
        return ESP_OK;  // No update needed
    }

    ESP_LOGI(TAG, "Update available: %s -> %s", app->version, server_version);
    return ESP_ERR_NOT_FOUND;  // Signal: update available
}

static esp_err_t perform_ota(void)
{
    build_ota_url();
    ESP_LOGI(TAG, "Starting OTA from: %s", s_ota_url);

    esp_http_client_config_t http_config = {
        .url = s_ota_url,
        .timeout_ms = 30000,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    s_state = OTA_STATE_DOWNLOADING;
    snprintf(s_status_msg, sizeof(s_status_msg), "Downloading...");

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        s_state = OTA_STATE_DONE;
        snprintf(s_status_msg, sizeof(s_status_msg), "Update complete, rebooting...");
        ESP_LOGI(TAG, "OTA successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        s_state = OTA_STATE_ERROR;
        snprintf(s_status_msg, sizeof(s_status_msg), "OTA failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void ota_task(void *arg)
{
    while (1) {
        xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
        s_state = OTA_STATE_CHECKING;
        snprintf(s_status_msg, sizeof(s_status_msg), "Checking for updates...");
        xSemaphoreGive(s_ota_mutex);

        esp_err_t ret = check_version();
        if (ret == ESP_ERR_NOT_FOUND) {
            // Update available
            xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
            perform_ota();
            xSemaphoreGive(s_ota_mutex);
        } else if (ret == ESP_OK) {
            xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
            s_state = OTA_STATE_IDLE;
            xSemaphoreGive(s_ota_mutex);
        } else {
            xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
            s_state = OTA_STATE_ERROR;
            snprintf(s_status_msg, sizeof(s_status_msg), "Check failed");
            xSemaphoreGive(s_ota_mutex);
        }

        if (!s_periodic_running) {
            // Manual check mode - exit after one check
            break;
        }

        // Sleep until next check or until woken by manual trigger
        xSemaphoreTake(s_wake_sem, pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS));
    }

    // Reset state so manual checks work again after task exits
    s_state = OTA_STATE_IDLE;
    snprintf(s_status_msg, sizeof(s_status_msg), "Idle");
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t ota_client_init(void)
{
    s_ota_mutex = xSemaphoreCreateMutex();
    if (s_ota_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create OTA mutex");
        return ESP_ERR_NO_MEM;
    }

    s_wake_sem = xSemaphoreCreateBinary();
    if (s_wake_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create wake semaphore");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA client initialized, server: %s", CONFIG_OTA_SERVER_URL);
    return ESP_OK;
}

esp_err_t ota_client_start_periodic(void)
{
    if (s_ota_task_handle != NULL) {
        ESP_LOGW(TAG, "OTA task already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_periodic_running = true;
    BaseType_t ret = xTaskCreate(ota_task, "ota_task", OTA_TASK_STACK_SIZE,
                                  NULL, tskIDLE_PRIORITY + 1, &s_ota_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Periodic OTA check started (every %d min)", OTA_CHECK_INTERVAL_MS / 60000);
    return ESP_OK;
}

esp_err_t ota_client_check_now(void)
{
    // Check if OTA is actually in progress (checking or downloading)
    if (ota_client_is_active()) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    // If periodic task is running but idle, wake it up immediately
    if (s_ota_task_handle != NULL && s_periodic_running) {
        ESP_LOGI(TAG, "Triggering immediate check");
        xSemaphoreGive(s_wake_sem);
        return ESP_OK;
    }

    // No task running, start a one-shot check
    s_periodic_running = false;
    BaseType_t ret = xTaskCreate(ota_task, "ota_task", OTA_TASK_STACK_SIZE,
                                  NULL, tskIDLE_PRIORITY + 2, &s_ota_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

ota_state_t ota_client_get_state(void)
{
    return s_state;
}

const char *ota_client_get_status(void)
{
    return s_status_msg;
}

bool ota_client_is_active(void)
{
    return s_state == OTA_STATE_DOWNLOADING || s_state == OTA_STATE_CHECKING;
}
