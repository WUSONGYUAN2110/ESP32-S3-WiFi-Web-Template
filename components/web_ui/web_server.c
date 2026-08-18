#include "web_server.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "web_api.h"
#include "wifi_manager.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server;
static SemaphoreHandle_t s_broadcast_lock;

extern const uint8_t web_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t web_index_html_end[] asm("_binary_index_html_end");
extern const uint8_t web_portal_html_start[] asm("_binary_portal_html_start");
extern const uint8_t web_portal_html_end[] asm("_binary_portal_html_end");
extern const uint8_t web_app_css_start[] asm("_binary_app_css_start");
extern const uint8_t web_app_css_end[] asm("_binary_app_css_end");
extern const uint8_t web_app_js_start[] asm("_binary_app_js_start");
extern const uint8_t web_app_js_end[] asm("_binary_app_js_end");
extern const uint8_t web_portal_js_start[] asm("_binary_portal_js_start");
extern const uint8_t web_portal_js_end[] asm("_binary_portal_js_end");

static esp_err_t send_embedded(httpd_req_t *req, const char *type,
                               const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)start, end - start);
}

size_t web_server_websocket_client_count(void)
{
    int fds[16];
    size_t count = sizeof(fds) / sizeof(fds[0]);
    if (s_server == NULL || httpd_get_client_list(s_server, &count, fds) != ESP_OK) {
        return 0;
    }

    size_t websocket_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            websocket_count++;
        }
    }
    return websocket_count;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    wifi_manager_status_t status;
    wifi_manager_get_status(&status);
    if (status.ap_active && status.phase != WIFI_PHASE_CONNECTED) {
        return send_embedded(req, "text/html; charset=utf-8",
                             web_portal_html_start, web_portal_html_end);
    }
    return send_embedded(req, "text/html; charset=utf-8",
                         web_index_html_start, web_index_html_end);
}

static esp_err_t css_handler(httpd_req_t *req)
{
    return send_embedded(req, "text/css; charset=utf-8", web_app_css_start, web_app_css_end);
}

static esp_err_t app_js_handler(httpd_req_t *req)
{
    return send_embedded(req, "application/javascript; charset=utf-8", web_app_js_start, web_app_js_end);
}

static esp_err_t portal_js_handler(httpd_req_t *req)
{
    return send_embedded(req, "application/javascript; charset=utf-8", web_portal_js_start, web_portal_js_end);
}

static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket client connected (fd=%d)", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK || frame.len == 0) {
        return err;
    }
    if (frame.len > 256) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *payload = calloc(1, frame.len + 1);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    free(payload);
    return err;
}

static esp_err_t captive_probe_handler(httpd_req_t *req)
{
    wifi_manager_status_t status;
    wifi_manager_get_status(&status);
    if (!status.ap_active) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t error)
{
    (void)error;
    wifi_manager_status_t status;
    wifi_manager_get_status(&status);
    if (status.ap_active) {
        return captive_probe_handler(req);
    }
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
}

void web_server_notify_state_changed(void)
{
    if (s_server == NULL || s_broadcast_lock == NULL ||
        xSemaphoreTake(s_broadcast_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        return;
    }

    char *body = web_api_create_status_json(web_server_websocket_client_count());
    if (body != NULL) {
        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)body,
            .len = strlen(body),
        };
        int fds[16];
        size_t count = sizeof(fds) / sizeof(fds[0]);
        if (httpd_get_client_list(s_server, &count, fds) == ESP_OK) {
            for (size_t i = 0; i < count; ++i) {
                if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                    httpd_ws_send_frame_async(s_server, fds[i], &frame);
                }
            }
        }
        free(body);
    }
    xSemaphoreGive(s_broadcast_lock);
}

static void status_broadcast_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        web_server_notify_state_changed();
    }
}

static esp_err_t register_uri(const char *uri, httpd_method_t method,
                              esp_err_t (*handler)(httpd_req_t *), bool websocket)
{
    const httpd_uri_t config = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .is_websocket = websocket,
    };
    return httpd_register_uri_handler(s_server, &config);
}

esp_err_t web_server_start(void)
{
    s_broadcast_lock = xSemaphoreCreateMutex();
    if (s_broadcast_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 24;
    config.max_open_sockets = 8;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 8;
    config.send_wait_timeout = 8;
    ESP_ERROR_CHECK(httpd_start(&s_server, &config));

    ESP_ERROR_CHECK(register_uri("/", HTTP_GET, root_handler, false));
    ESP_ERROR_CHECK(register_uri("/app.css", HTTP_GET, css_handler, false));
    ESP_ERROR_CHECK(register_uri("/app.js", HTTP_GET, app_js_handler, false));
    ESP_ERROR_CHECK(register_uri("/portal.js", HTTP_GET, portal_js_handler, false));
    ESP_ERROR_CHECK(register_uri("/ws", HTTP_GET, websocket_handler, true));
    ESP_ERROR_CHECK(register_uri("/generate_204", HTTP_GET, captive_probe_handler, false));
    ESP_ERROR_CHECK(register_uri("/hotspot-detect.html", HTTP_GET, captive_probe_handler, false));
    ESP_ERROR_CHECK(register_uri("/connecttest.txt", HTTP_GET, captive_probe_handler, false));
    ESP_ERROR_CHECK(register_uri("/ncsi.txt", HTTP_GET, captive_probe_handler, false));
    ESP_ERROR_CHECK(register_uri("/canonical.html", HTTP_GET, captive_probe_handler, false));
    ESP_ERROR_CHECK(register_uri("/success.txt", HTTP_GET, captive_probe_handler, false));
    ESP_ERROR_CHECK(web_api_register(s_server));
    ESP_ERROR_CHECK(httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, not_found_handler));

    if (xTaskCreate(status_broadcast_task, "web_status", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "HTTP and WebSocket server started");
    return ESP_OK;
}
