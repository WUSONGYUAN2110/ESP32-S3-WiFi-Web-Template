idf_component_register(
    SRCS "app_main.c"
    PRIV_REQUIRES
        nvs_flash
        provision_button
        wifi_manager
        web_ui
        ws2812_led
)
