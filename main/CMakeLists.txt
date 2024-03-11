set(esp_http_client json freertos esp_wifi esp_system driver esp_event esp_log nvs_flash esp_common esp_netif esp_hw_support)

idf_component_register(
    SRCS  "test2.c"  
    INCLUDE_DIRS "."
    REQUIRES ${pri_req}
)


target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
