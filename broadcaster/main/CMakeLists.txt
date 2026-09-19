idf_component_register(
    SRCS "app_main.c"
         "wifi_sta.c"
         "espnow_ctrl.c"
         "beacon_tx.c"
         "ota_client.c"
         "led.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_driver_gpio esp_https_ota esp_app_format bootloader_support mbedtls
             nvs_flash esp_timer esp_netif esp_event
             espnow_proto
)
