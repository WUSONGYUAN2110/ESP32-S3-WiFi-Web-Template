idf_component_register(
    SRCS
        "web_server.c"
        "web_api.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_http_server
        esp_timer
        wifi_manager
        ws2812_led
    EMBED_FILES
        "web/index.html"
        "web/portal.html"
        "web/app.css"
        "web/app.js"
        "web/portal.js"
)
