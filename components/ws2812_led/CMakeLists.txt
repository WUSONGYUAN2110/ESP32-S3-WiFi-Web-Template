idf_component_register(
    SRCS
        "ws2812_led.c"
        "ws2812_encoder.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_rmt
)
