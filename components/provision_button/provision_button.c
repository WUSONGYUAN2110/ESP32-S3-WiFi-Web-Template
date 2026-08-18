#include "provision_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PROVISION_BUTTON_GPIO GPIO_NUM_0
#define PROVISION_BUTTON_HOLD_MS 5000
#define PROVISION_BUTTON_POLL_MS 50

static provision_button_callback_t s_callback;

static void provision_button_task(void *arg)
{
    (void)arg;
    uint32_t held_ms = 0;
    bool callback_sent = false;

    while (true) {
        if (gpio_get_level(PROVISION_BUTTON_GPIO) == 0) {
            if (!callback_sent) {
                held_ms += PROVISION_BUTTON_POLL_MS;
                if (held_ms >= PROVISION_BUTTON_HOLD_MS) {
                    callback_sent = true;
                    s_callback();
                }
            }
        } else {
            held_ms = 0;
            callback_sent = false;
        }
        vTaskDelay(pdMS_TO_TICKS(PROVISION_BUTTON_POLL_MS));
    }
}

esp_err_t provision_button_start(provision_button_callback_t callback)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_callback = callback;

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << PROVISION_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(provision_button_task, "provision_button", 3072,
                    NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
