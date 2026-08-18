#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

/** Register every /api route on an already running HTTP server. */
esp_err_t web_api_register(httpd_handle_t server);

/**
 * Build the shared status payload used by REST and WebSocket.
 * The caller owns the returned heap string and must free it.
 */
char *web_api_create_status_json(size_t websocket_clients);
