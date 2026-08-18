#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "provision_button.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "ws2812_led.h"

static const char *TAG = "wifi_web_template";

static void handle_provision_button(void)
{
    ESP_LOGW(TAG, "BOOT held for 5 seconds: clearing Wi-Fi credentials");
    esp_err_t err = wifi_manager_enter_provisioning(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset Wi-Fi: %s", esp_err_to_name(err));
    }
}

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    initialize_nvs();
    ESP_ERROR_CHECK(ws2812_led_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(web_server_start());

    wifi_manager_set_change_callback(web_server_notify_state_changed);
    ESP_ERROR_CHECK(provision_button_start(handle_provision_button));
    ESP_LOGI(TAG, "ESP32-S3 Wi-Fi web template is ready");
}
