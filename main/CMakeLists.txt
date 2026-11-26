file(GLOB APP_SRCS "*.c" "apps/*.c" "fonts/*.c")

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "." "apps"
    EMBED_TXTFILES "www/upload.html" "www/wifi.html"
    REQUIRES lvgl esp_new_jpeg esp_lcd driver esp_wifi esp_netif esp_event nvs_flash
             esp_http_server esp_timer fatfs sdmmc esp_driver_sdmmc app_update esp_app_format
             esp_https_ota esp_http_client mbedtls)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
