idf_component_register(SRCS "main.c" "audio_kit.c" "es8388.c" "dns_server.c" "web_server.c" "wifi_manager.c" "web_radio.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_i2s esp_driver_i2c esp_driver_gpio nvs_flash esp_timer esp_wifi esp_netif esp_http_server esp_http_client esp_ringbuf lwip json esp_event)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)

