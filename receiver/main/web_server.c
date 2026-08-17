/*
 * Web Server - Receiver
 * HTTP server for web UI, session control, file download, and OTA
 */

#include "web_server.h"
#include "session_store.h"
#include "csi_capture.h"
#include "espnow_ctrl.h"
#include "ota_client.h"
#include "wifi_sta.h"
#include "led.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "web_server";

static httpd_handle_t s_server = NULL;

// Embedded web UI
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// Session state
static bool s_session_running = false;
static char s_session_label[SESSION_LABEL_MAX] = {0};
static uint8_t s_session_channel = 0;
static uint16_t s_session_rate = 0;
static uint32_t s_session_duration = 0;
static int64_t s_session_start_time = 0;

// CSI callback for web server
static void csi_data_handler(const char *csv_line, int len)
{
    if (session_store_is_active()) {
        session_store_append(csv_line, len);
    }
}

// GET / - Serve web UI
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start,
                    index_html_end - index_html_start);
    return ESP_OK;
}

// GET /api/status - Get system status
static esp_err_t status_get_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "hostname", CONFIG_MDNS_HOSTNAME);
    cJSON_AddNumberToObject(root, "channel", wifi_sta_get_channel());
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_sta_is_connected());
    cJSON_AddBoolToObject(root, "broadcaster_paired", espnow_ctrl_is_paired());
    cJSON_AddBoolToObject(root, "session_running", s_session_running);
    cJSON_AddStringToObject(root, "session_label", s_session_label);
    cJSON_AddNumberToObject(root, "session_channel", s_session_channel);
    cJSON_AddNumberToObject(root, "session_rate", s_session_rate);
    cJSON_AddNumberToObject(root, "session_duration", s_session_duration);

    if (s_session_running) {
        int64_t elapsed = (esp_timer_get_time() - s_session_start_time) / 1000000;
        cJSON_AddNumberToObject(root, "session_elapsed", elapsed);
        cJSON_AddNumberToObject(root, "frames_captured", csi_capture_get_count());
    }

    cJSON_AddNumberToObject(root, "storage_free", session_store_get_free_space());
    cJSON_AddStringToObject(root, "ota_state", ota_client_get_status());

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(mac));
    cJSON_AddStringToObject(root, "mac", mac_str);

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_Delete(root);
    free(json_str);
    return ESP_OK;
}

// POST /api/session/start - Start a new session
static esp_err_t session_start_handler(httpd_req_t *req)
{
    if (s_session_running) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Session already running");
        return ESP_FAIL;
    }

    if (!espnow_ctrl_is_paired()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Broadcaster not paired");
        return ESP_FAIL;
    }

    // Parse JSON body
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *label = cJSON_GetObjectItem(root, "label");
    cJSON *channel = cJSON_GetObjectItem(root, "channel");
    cJSON *rate = cJSON_GetObjectItem(root, "rate");
    cJSON *duration = cJSON_GetObjectItem(root, "duration");

    if (!cJSON_IsString(label) || !cJSON_IsNumber(channel) ||
        !cJSON_IsNumber(rate) || !cJSON_IsNumber(duration)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
        return ESP_FAIL;
    }

    strncpy(s_session_label, label->valuestring, sizeof(s_session_label) - 1);
    s_session_channel = (uint8_t)channel->valueint;
    s_session_rate = (uint16_t)rate->valueint;
    s_session_duration = (uint32_t)duration->valueint;

    cJSON_Delete(root);

    // Start session
    esp_err_t err = session_store_begin(s_session_label, s_session_channel, s_session_rate);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start session");
        return ESP_FAIL;
    }

    // Switch to work channel
    wifi_sta_set_channel(s_session_channel);

    // Start CSI capture
    err = csi_capture_start(s_session_channel, csi_data_handler);
    if (err != ESP_OK) {
        session_store_end();
        wifi_sta_restore_channel();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start CSI capture");
        return ESP_FAIL;
    }

    // Send START to broadcaster
    espnow_start_t start_msg = {
        .magic = ESPNOW_PROTO_MAGIC,
        .cmd = ESPNOW_CMD_START,
        .channel = s_session_channel,
        .rate_hz = s_session_rate,
        .duration_s = s_session_duration,
    };
    snprintf((char *)start_msg.session_id, sizeof(start_msg.session_id), "%s", session_store_get_id());

    err = espnow_ctrl_send_start(&start_msg);
    if (err != ESP_OK) {
        csi_capture_stop();
        session_store_end();
        wifi_sta_restore_channel();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send START");
        return ESP_FAIL;
    }

    s_session_running = true;
    s_session_start_time = esp_timer_get_time();
    led_blink(100, 100);  // Fast blink during session

    ESP_LOGI(TAG, "Session started: %s ch=%d rate=%dHz dur=%ds",
             s_session_label, s_session_channel, s_session_rate, s_session_duration);

    httpd_resp_send(req, "{\"status\":\"ok\"}", strlen("{\"status\":\"ok\"}"));
    return ESP_OK;
}

// POST /api/session/stop - Stop current session
static esp_err_t session_stop_handler(httpd_req_t *req)
{
    if (!s_session_running) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No session running");
        return ESP_FAIL;
    }

    // Send STOP to broadcaster
    espnow_stop_t stop_msg = {
        .magic = ESPNOW_PROTO_MAGIC,
        .cmd = ESPNOW_CMD_STOP,
    };
    const char *sid = session_store_get_id();
    if (sid) {
        memcpy(stop_msg.session_id, sid, SESSION_ID_LEN);
    }
    espnow_ctrl_send_stop(&stop_msg);

    // Stop CSI capture
    csi_capture_stop();

    // Close session file
    session_store_end();

    // Restore Wi-Fi channel
    wifi_sta_restore_channel();

    s_session_running = false;
    led_stop_blink();
    led_on();  // Solid on when idle

    ESP_LOGI(TAG, "Session stopped");

    httpd_resp_send(req, "{\"status\":\"ok\"}", strlen("{\"status\":\"ok\"}"));
    return ESP_OK;
}

// GET /api/sessions - List all sessions
static esp_err_t sessions_list_handler(httpd_req_t *req)
{
    char buf[4096];
    esp_err_t err = session_store_list(buf, sizeof(buf));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to list sessions");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

// GET /api/sessions/download?file=xxx - Download session CSV
static esp_err_t session_download_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_get_url_query_var(req, "file", buf, sizeof(buf));
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file parameter");
        return ESP_FAIL;
    }

    char path[256];
    esp_err_t err = session_store_get_path(buf, path, sizeof(path));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment");

    char chunk[1024];
    size_t read;
    while ((read = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        httpd_resp_send_chunk(req, chunk, read);
    }
    httpd_resp_send_chunk(req, NULL, 0);

    fclose(f);
    return ESP_OK;
}

// DELETE /api/sessions?file=xxx - Delete session
static esp_err_t session_delete_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_get_url_query_var(req, "file", buf, sizeof(buf));
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file parameter");
        return ESP_FAIL;
    }

    esp_err_t err = session_store_delete(buf);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete");
        return ESP_FAIL;
    }

    httpd_resp_send(req, "{\"status\":\"ok\"}", strlen("{\"status\":\"ok\"}"));
    return ESP_OK;
}

// POST /api/ota/check - Trigger manual OTA check
static esp_err_t ota_check_handler(httpd_req_t *req)
{
    esp_err_t err = ota_client_check_now();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "OTA already in progress");
        return ESP_FAIL;
    }

    httpd_resp_send(req, "{\"status\":\"ok\"}", strlen("{\"status\":\"ok\"}"));
    return ESP_OK;
}

esp_err_t web_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_register_uri_handler(s_server, &root_uri);

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
    };
    httpd_register_uri_handler(s_server, &status_uri);

    httpd_uri_t session_start_uri = {
        .uri = "/api/session/start",
        .method = HTTP_POST,
        .handler = session_start_handler,
    };
    httpd_register_uri_handler(s_server, &session_start_uri);

    httpd_uri_t session_stop_uri = {
        .uri = "/api/session/stop",
        .method = HTTP_POST,
        .handler = session_stop_handler,
    };
    httpd_register_uri_handler(s_server, &session_stop_uri);

    httpd_uri_t sessions_list_uri = {
        .uri = "/api/sessions",
        .method = HTTP_GET,
        .handler = sessions_list_handler,
    };
    httpd_register_uri_handler(s_server, &sessions_list_uri);

    httpd_uri_t session_download_uri = {
        .uri = "/api/sessions/download",
        .method = HTTP_GET,
        .handler = session_download_handler,
    };
    httpd_register_uri_handler(s_server, &session_download_uri);

    httpd_uri_t session_delete_uri = {
        .uri = "/api/sessions",
        .method = HTTP_DELETE,
        .handler = session_delete_handler,
    };
    httpd_register_uri_handler(s_server, &session_delete_uri);

    httpd_uri_t ota_check_uri = {
        .uri = "/api/ota/check",
        .method = HTTP_POST,
        .handler = ota_check_handler,
    };
    httpd_register_uri_handler(s_server, &ota_check_uri);

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
