idf_component_register(
    SRCS "app_main.c"
         "wifi_sta.c"
         "csi_capture.c"
         "session_store.c"
         "espnow_ctrl.c"
         "ota_client.c"
         "web_server.c"
         "led.c"
    INCLUDE_DIRS "."
    EMBED_FILES "www/index.html"
    REQUIRES esp_wifi esp_http_server esp_https_ota esp_app_format
             nvs_flash fatfs esp_timer esp_netif esp_event mdns cjson
             bootloader_support mbedtls espnow_proto
)
