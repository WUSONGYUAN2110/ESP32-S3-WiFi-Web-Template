#include "web_api.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "web_server.h"
#include "web_server_internal.h"
#include "wifi_manager.h"
#include "ws2812_led.h"

#define MAX_REQUEST_BODY 512
#define MAX_MESSAGE_BYTES 96

static const char *TAG = "web_api";
static SemaphoreHandle_t s_message_lock;
static char s_message[MAX_MESSAGE_BYTES + 1] = "欢迎使用 ESP32-S3 网页交互模板";

// ---- HTTP and JSON helpers -----------------------------------------------------

static esp_err_t send_json_text(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddFalseToObject(root, "ok");
    cJSON_AddStringToObject(root, "error", message);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
    }
    esp_err_t err = send_json_text(req, status, body);
    free(body);
    return err;
}

static cJSON *read_json_body(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > MAX_REQUEST_BODY) {
        return NULL;
    }
    char *buffer = calloc(1, req->content_len + 1);
    if (buffer == NULL) {
        return NULL;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int result = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (result <= 0) {
            free(buffer);
            return NULL;
        }
        received += result;
    }
    cJSON *json = cJSON_ParseWithLength(buffer, received);
    free(buffer);
    return json;
}

// ---- Shared REST/WebSocket status model ---------------------------------------

char *web_api_create_status_json(size_t websocket_clients)
{
    wifi_manager_status_t wifi;
    wifi_manager_get_status(&wifi);
    ws2812_led_state_t led;
    ws2812_led_get(&led);

    char color[8];
    snprintf(color, sizeof(color), "#%02X%02X%02X", led.red, led.green, led.blue);
    char message[MAX_MESSAGE_BYTES + 1];
    xSemaphoreTake(s_message_lock, portMAX_DELAY);
    strlcpy(message, s_message, sizeof(message));
    xSemaphoreGive(s_message_lock);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddStringToObject(root, "phase", wifi_manager_phase_name(wifi.phase));
    cJSON_AddBoolToObject(root, "connected", wifi.connected);
    cJSON_AddBoolToObject(root, "ap_active", wifi.ap_active);
    cJSON_AddBoolToObject(root, "has_saved_credentials", wifi.has_saved_credentials);
    cJSON_AddStringToObject(root, "ssid", wifi.ssid);
    cJSON_AddStringToObject(root, "ip", wifi.ip);
    cJSON_AddStringToObject(root, "ap_ssid", wifi.ap_ssid);
    cJSON_AddNumberToObject(root, "rssi_dbm", wifi.connected ? wifi.rssi : 0);
    cJSON_AddStringToObject(root, "error", wifi.error);
    cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000ULL);
    cJSON_AddNumberToObject(root, "free_heap_bytes", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "ws_clients", websocket_clients);
    cJSON_AddStringToObject(root, "message", message);

    cJSON *led_json = cJSON_AddObjectToObject(root, "led");
    cJSON_AddBoolToObject(led_json, "on", led.on);
    cJSON_AddStringToObject(led_json, "color", color);
    cJSON_AddNumberToObject(led_json, "brightness", led.brightness);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char *body = web_api_create_status_json(web_server_websocket_client_count());
    if (body == NULL) {
        return send_json_error(req, "500 Internal Server Error", "状态序列化失败");
    }
    esp_err_t err = send_json_text(req, "200 OK", body);
    free(body);
    return err;
}

// ---- Wi-Fi API ----------------------------------------------------------------

static const char *security_name(wifi_security_t security)
{
    switch (security) {
    case WIFI_SECURITY_OPEN: return "开放";
    case WIFI_SECURITY_WEP: return "WEP";
    case WIFI_SECURITY_WPA: return "WPA";
    case WIFI_SECURITY_WPA2: return "WPA2";
    case WIFI_SECURITY_WPA_WPA2: return "WPA/WPA2";
    case WIFI_SECURITY_WPA3: return "WPA3";
    case WIFI_SECURITY_WPA2_WPA3: return "WPA2/WPA3";
    default: return "加密";
    }
}

static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    wifi_scan_result_t records[WIFI_MANAGER_MAX_SCAN_RESULTS] = {0};
    uint16_t count = WIFI_MANAGER_MAX_SCAN_RESULTS;
    esp_err_t err = wifi_manager_scan(records, &count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        return send_json_error(req, "409 Conflict", "当前无法扫描，请稍后重试");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddTrueToObject(root, "ok");
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    for (uint16_t i = 0; i < count; ++i) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        bool duplicate = false;
        for (uint16_t j = 0; j < i; ++j) {
            if (strcmp(records[i].ssid, records[j].ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", records[i].rssi);
        cJSON_AddStringToObject(network, "security", security_name(records[i].security));
        cJSON_AddBoolToObject(network, "open", records[i].security == WIFI_SECURITY_OPEN);
        cJSON_AddItemToArray(networks, network);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return send_json_error(req, "500 Internal Server Error", "扫描结果序列化失败");
    }
    err = send_json_text(req, "200 OK", body);
    free(body);
    return err;
}

static esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    cJSON *json = read_json_body(req);
    if (json == NULL) {
        return send_json_error(req, "400 Bad Request", "请求必须是有效且不超过 512 字节的 JSON");
    }
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
        cJSON_Delete(json);
        return send_json_error(req, "400 Bad Request", "ssid 和 password 必须是字符串");
    }

    esp_err_t err = wifi_manager_submit_credentials(ssid->valuestring, password->valuestring);
    cJSON_Delete(json);
    if (err == ESP_ERR_INVALID_ARG) {
        return send_json_error(req, "400 Bad Request", "SSID 长度应为 1–32 字节；密码应为空或 8–63 字节");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "已有网络正在验证，请等待结果");
    }
    if (err != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "无法启动网络验证");
    }
    return send_json_text(req, "202 Accepted", "{\"ok\":true,\"state\":\"testing\"}");
}

static void reprovision_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_err_t err = wifi_manager_enter_provisioning(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter provisioning: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

static esp_err_t reprovision_handler(httpd_req_t *req)
{
    if (xTaskCreate(reprovision_task, "reprovision", 4096, NULL, 5, NULL) != pdPASS) {
        return send_json_error(req, "500 Internal Server Error", "无法创建重新配网任务");
    }
    return send_json_text(req, "202 Accepted",
                          "{\"ok\":true,\"message\":\"设备即将开启配网热点\"}");
}

// ---- Device control API --------------------------------------------------------

static bool parse_hex_color(const char *text, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    if (text == NULL || strlen(text) != 7 || text[0] != '#') {
        return false;
    }
    for (int i = 1; i < 7; ++i) {
        if (!isxdigit((unsigned char)text[i])) {
            return false;
        }
    }
    unsigned value;
    if (sscanf(text + 1, "%06x", &value) != 1) {
        return false;
    }
    *red = (value >> 16) & 0xff;
    *green = (value >> 8) & 0xff;
    *blue = value & 0xff;
    return true;
}

static esp_err_t led_handler(httpd_req_t *req)
{
    cJSON *json = read_json_body(req);
    if (json == NULL) {
        return send_json_error(req, "400 Bad Request", "无效 JSON");
    }
    cJSON *on = cJSON_GetObjectItemCaseSensitive(json, "on");
    cJSON *color = cJSON_GetObjectItemCaseSensitive(json, "color");
    cJSON *brightness = cJSON_GetObjectItemCaseSensitive(json, "brightness");
    ws2812_led_state_t state = {0};
    bool valid = cJSON_IsBool(on) && cJSON_IsString(color) && cJSON_IsNumber(brightness) &&
                 brightness->valuedouble >= 0 && brightness->valuedouble <= 100 &&
                 brightness->valuedouble == brightness->valueint &&
                 parse_hex_color(color->valuestring, &state.red, &state.green, &state.blue);
    if (!valid) {
        cJSON_Delete(json);
        return send_json_error(req, "400 Bad Request", "需要 on、#RRGGBB color 和 0–100 整数 brightness");
    }
    state.on = cJSON_IsTrue(on);
    state.brightness = brightness->valueint;
    cJSON_Delete(json);

    if (ws2812_led_set(&state) != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "灯光更新失败");
    }
    web_server_notify_state_changed();
    return send_json_text(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t message_handler(httpd_req_t *req)
{
    cJSON *json = read_json_body(req);
    if (json == NULL) {
        return send_json_error(req, "400 Bad Request", "无效 JSON");
    }
    cJSON *message = cJSON_GetObjectItemCaseSensitive(json, "message");
    if (!cJSON_IsString(message) || strlen(message->valuestring) > MAX_MESSAGE_BYTES) {
        cJSON_Delete(json);
        return send_json_error(req, "400 Bad Request", "message 必须是不超过 96 字节的字符串");
    }
    xSemaphoreTake(s_message_lock, portMAX_DELAY);
    strlcpy(s_message, message->valuestring, sizeof(s_message));
    xSemaphoreGive(s_message_lock);
    cJSON_Delete(json);
    web_server_notify_state_changed();
    return send_json_text(req, "200 OK", "{\"ok\":true}");
}

// ---- Route registration --------------------------------------------------------

static esp_err_t register_uri(httpd_handle_t server, const char *uri,
                              httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    const httpd_uri_t config = {
        .uri = uri,
        .method = method,
        .handler = handler,
    };
    return httpd_register_uri_handler(server, &config);
}

esp_err_t web_api_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_message_lock = xSemaphoreCreateMutex();
    if (s_message_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(register_uri(server, "/api/status", HTTP_GET, status_handler));
    ESP_ERROR_CHECK(register_uri(server, "/api/wifi/state", HTTP_GET, status_handler));
    ESP_ERROR_CHECK(register_uri(server, "/api/wifi/scan", HTTP_GET, wifi_scan_handler));
    ESP_ERROR_CHECK(register_uri(server, "/api/wifi/connect", HTTP_POST, wifi_connect_handler));
    ESP_ERROR_CHECK(register_uri(server, "/api/wifi/reprovision", HTTP_POST, reprovision_handler));
    ESP_ERROR_CHECK(register_uri(server, "/api/led", HTTP_PUT, led_handler));
    ESP_ERROR_CHECK(register_uri(server, "/api/message", HTTP_PUT, message_handler));
    return ESP_OK;
}
