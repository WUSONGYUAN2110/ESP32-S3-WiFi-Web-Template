#include "ws2812_led.h"

#include <string.h>

#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ws2812_encoder.h"

#define LED_GPIO 48
#define LED_RMT_RESOLUTION_HZ 10000000

static SemaphoreHandle_t s_lock;
static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static ws2812_led_state_t s_state;

static esp_err_t transmit_locked(void)
{
    uint8_t pixels[3] = {0};
    if (s_state.on) {
        pixels[0] = (uint8_t)(((uint16_t)s_state.green * s_state.brightness) / 100U);
        pixels[1] = (uint8_t)(((uint16_t)s_state.red * s_state.brightness) / 100U);
        pixels[2] = (uint8_t)(((uint16_t)s_state.blue * s_state.brightness) / 100U);
    }

    const rmt_transmit_config_t tx_config = {.loop_count = 0};
    esp_err_t err = rmt_transmit(s_channel, s_encoder, pixels, sizeof(pixels), &tx_config);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s_channel, pdMS_TO_TICKS(100));
    }
    return err;
}

esp_err_t ws2812_led_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = LED_RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&channel_config, &s_channel), "led", "create RMT channel");

    const led_strip_encoder_config_t encoder_config = {
        .resolution = LED_RMT_RESOLUTION_HZ,
    };
    ESP_RETURN_ON_ERROR(rmt_new_led_strip_encoder(&encoder_config, &s_encoder), "led", "create encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_channel), "led", "enable channel");

    s_state = (ws2812_led_state_t){
        .on = false,
        .red = 42,
        .green = 124,
        .blue = 255,
        .brightness = 60,
    };
    return transmit_locked();
}

esp_err_t ws2812_led_set(const ws2812_led_state_t *state)
{
    if (state == NULL || state->brightness > 100 || s_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state = *state;
    esp_err_t err = transmit_locked();
    xSemaphoreGive(s_lock);
    return err;
}

void ws2812_led_get(ws2812_led_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (s_lock == NULL) {
        memset(state, 0, sizeof(*state));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *state = s_state;
    xSemaphoreGive(s_lock);
}
