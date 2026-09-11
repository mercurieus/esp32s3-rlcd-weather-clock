idf_component_register(
    SRCS "clock_time.c" "clock_time_test.c" "pcf85063.c" "wifi_sync.c" "clock_task.c" "shtc3.c" "battery.c" "weather.c" "wifi_stress.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES main driver esp_driver_usb_serial_jtag esp_wifi esp_netif esp_event nvs_flash lwip ui app_bsp esp_adc esp_http_client esp-tls json esp_timer selftest input
)