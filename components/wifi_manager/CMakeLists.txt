idf_component_register(
    SRCS
        "wifi_manager.c"
        "wifi_credentials.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        dns_server
        esp_event
        esp_netif
        esp_timer
        esp_wifi
        lwip
        nvs_flash
)
