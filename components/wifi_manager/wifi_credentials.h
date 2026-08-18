#pragma once

#include <stddef.h>

#include "esp_err.h"

/**
 * @brief Load the active Wi-Fi credentials from the component-owned NVS namespace.
 *
 * Output buffers are cleared when no complete credential pair exists.
 */
esp_err_t wifi_credentials_load(char *ssid, size_t ssid_size,
                                char *password, size_t password_size);

/** Save a validated credential pair atomically with one NVS commit. */
esp_err_t wifi_credentials_save(const char *ssid, const char *password);

/** Remove every key owned by the Wi-Fi credential store. */
esp_err_t wifi_credentials_clear(void);
