#pragma once

#include "esp_err.h"

/** Start the embedded assets, REST API, captive routes and WebSocket endpoint. */
esp_err_t web_server_start(void);

/** Broadcast current state; suitable for use as a wifi_manager change callback. */
void web_server_notify_state_changed(void);
