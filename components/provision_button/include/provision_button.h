#pragma once

#include "esp_err.h"

typedef void (*provision_button_callback_t)(void);

/**
 * Configure the runtime BOOT-button monitor and invoke callback after a
 * continuous five-second press. The callback runs in the monitor task.
 */
esp_err_t provision_button_start(provision_button_callback_t callback);
