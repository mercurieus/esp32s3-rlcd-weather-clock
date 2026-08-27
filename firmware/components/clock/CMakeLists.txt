idf_component_register(
    SRCS "clock_time.c" "pcf85063.c" "wifi_sync.c" "clock_task.c" "shtc3.c" "battery.c" "weather.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES main driver esp_wifi esp_netif esp_event nvs_flash lwip ui app_bsp esp_adc esp_http_client esp-tls json esp_timer
)