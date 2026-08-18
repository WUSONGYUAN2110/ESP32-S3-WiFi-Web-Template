#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool on;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
} ws2812_led_state_t;

/** Initialize the GPIO48 RMT channel and leave the LED off. */
esp_err_t ws2812_led_init(void);

/** Apply one complete state atomically; brightness is limited to 0-100. */
esp_err_t ws2812_led_set(const ws2812_led_state_t *state);

/** Copy the current thread-safe state snapshot. */
void ws2812_led_get(ws2812_led_state_t *state);
