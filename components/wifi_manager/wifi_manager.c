#include "wifi_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dns_server.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "wifi_credentials.h"

static const char *TAG = "wifi_manager";
static const uint32_t s_backoff_seconds[] = {1, 2, 4, 8, 16, 30};
static const uint64_t CANDIDATE_START_DELAY_US = 500000ULL;

static SemaphoreHandle_t s_lock;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_fallback_timer;
static esp_timer_handle_t s_candidate_start_timer;
static esp_timer_handle_t s_candidate_timer;
static esp_timer_handle_t s_ap_shutdown_timer;
static dns_server_handle_t s_dns_server;
static wifi_manager_change_cb_t s_change_callback;
static wifi_manager_status_t s_status;
static char s_saved_ssid[33];
static char s_saved_password[65];
static char s_candidate_ssid[33];
static char s_candidate_password[65];
static bool s_candidate_validation_started;
static size_t s_backoff_index;

// ---- Shared state notification ------------------------------------------------

static void notify_changed(void)
{
    wifi_manager_change_cb_t callback = s_change_callback;
    if (callback != NULL) {
        callback();
    }
}

static esp_err_t apply_station_config(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

// ---- Provisioning access point ------------------------------------------------

static void configure_ap_ip(void)
{
    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(s_ap_netif);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));
}

static esp_err_t start_provisioning_ap(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool already_active = s_status.ap_active;
    xSemaphoreGive(s_lock);
    if (already_active) {
        return ESP_OK;
    }

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    char ap_ssid[33];
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP32S3-Setup-%02X%02X", mac[4], mac[5]);

    const char *password = CONFIG_WIFI_WEB_SETUP_PASSWORD;
    size_t password_len = strlen(password);
    if (password_len < 8 || password_len > 63) {
        ESP_LOGW(TAG, "Invalid setup AP password length; using built-in fallback");
        password = "esp32setup";
    }

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, password, sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 4;
    ap_config.ap.pmf_cfg.capable = true;
    ap_config.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    configure_ap_ip();

    dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    s_dns_server = start_dns_server(&dns_config);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.ap_active = true;
    strlcpy(s_status.ap_ssid, ap_ssid, sizeof(s_status.ap_ssid));
    if (!s_status.connected && !s_status.candidate_pending) {
        s_status.phase = WIFI_PHASE_PROVISIONING;
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Provisioning AP ready: %s, http://192.168.4.1", ap_ssid);
    notify_changed();
    return ESP_OK;
}

static void stop_provisioning_ap(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool active = s_status.ap_active;
    xSemaphoreGive(s_lock);
    if (!active) {
        return;
    }

    if (s_dns_server != NULL) {
        stop_dns_server(s_dns_server);
        s_dns_server = NULL;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.ap_active = false;
    s_status.ap_ssid[0] = '\0';
    if (s_status.connected) {
        s_status.phase = WIFI_PHASE_CONNECTED;
    }
    xSemaphoreGive(s_lock);
    notify_changed();
}

// ---- Retry and validation timers ----------------------------------------------

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reconnect request failed: %s", esp_err_to_name(err));
    }
}

static void fallback_timer_cb(void *arg)
{
    (void)arg;
    start_provisioning_ap();
}

static void ap_shutdown_timer_cb(void *arg)
{
    (void)arg;
    stop_provisioning_ap();
}

static void schedule_reconnect(void)
{
    uint32_t seconds = s_backoff_seconds[s_backoff_index];
    if (s_backoff_index + 1 < sizeof(s_backoff_seconds) / sizeof(s_backoff_seconds[0])) {
        s_backoff_index++;
    }
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)seconds * 1000000ULL);
    ESP_LOGI(TAG, "Wi-Fi reconnect in %" PRIu32 " second(s)", seconds);
}

static void restore_saved_after_candidate_failure(const char *reason)
{
    esp_timer_stop(s_candidate_start_timer);
    esp_timer_stop(s_candidate_timer);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.candidate_pending = false;
    s_candidate_validation_started = false;
    s_status.phase = WIFI_PHASE_FAILED;
    strlcpy(s_status.error, reason, sizeof(s_status.error));
    memset(s_candidate_ssid, 0, sizeof(s_candidate_ssid));
    memset(s_candidate_password, 0, sizeof(s_candidate_password));
    bool has_saved = s_saved_ssid[0] != '\0';
    xSemaphoreGive(s_lock);

    if (has_saved) {
        apply_station_config(s_saved_ssid, s_saved_password);
        esp_wifi_connect();
    }
    notify_changed();
}

static void candidate_start_timer_cb(void *arg)
{
    (void)arg;
    char ssid[sizeof(s_candidate_ssid)];
    char password[sizeof(s_candidate_password)];

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool pending = s_status.candidate_pending;
    strlcpy(ssid, s_candidate_ssid, sizeof(ssid));
    strlcpy(password, s_candidate_password, sizeof(password));
    xSemaphoreGive(s_lock);
    if (!pending) {
        memset(password, 0, sizeof(password));
        return;
    }

    ESP_LOGI(TAG, "Starting candidate Wi-Fi validation: SSID=%s", ssid);
    esp_err_t err = start_provisioning_ap();
    if (err == ESP_OK) {
        err = apply_station_config(ssid, password);
    }
    if (err == ESP_OK) {
        // A disconnect is expected to fail when provisioning starts without
        // an active STA link; either way the following connect uses the new
        // RAM-only candidate configuration.
        esp_wifi_disconnect();
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    pending = s_status.candidate_pending;
    if (pending && err == ESP_OK) {
        s_candidate_validation_started = true;
    }
    xSemaphoreGive(s_lock);

    if (pending && err == ESP_OK) {
        esp_timer_stop(s_candidate_timer);
        err = esp_timer_start_once(s_candidate_timer,
                                   (uint64_t)CONFIG_WIFI_WEB_CANDIDATE_TIMEOUT_SECONDS * 1000000ULL);
    }
    if (pending && err == ESP_OK) {
        err = esp_wifi_connect();
    }

    memset(password, 0, sizeof(password));
    if (pending && err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to start candidate Wi-Fi validation: %s", esp_err_to_name(err));
        restore_saved_after_candidate_failure("无法启动 Wi-Fi 连接");
    }
}

static void candidate_timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool pending = s_status.candidate_pending;
    xSemaphoreGive(s_lock);
    if (pending) {
        esp_wifi_disconnect();
        restore_saved_after_candidate_failure("连接超时，请检查 Wi-Fi 名称和密码");
    }
}

// ---- ESP-IDF event handling ----------------------------------------------------

static void handle_got_ip(const ip_event_got_ip_t *event)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool candidate_queued = s_status.candidate_pending;
    bool candidate_started = s_candidate_validation_started;
    xSemaphoreGive(s_lock);
    if (candidate_queued && !candidate_started) {
        ESP_LOGD(TAG, "Ignoring STA IP event while candidate validation is queued");
        return;
    }

    esp_timer_stop(s_reconnect_timer);
    esp_timer_stop(s_fallback_timer);
    s_backoff_index = 0;

    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool candidate = s_status.candidate_pending;
    s_status.connected = true;
    s_status.error[0] = '\0';
    strlcpy(s_status.ip, ip, sizeof(s_status.ip));
    if (candidate) {
        strlcpy(s_saved_ssid, s_candidate_ssid, sizeof(s_saved_ssid));
        strlcpy(s_saved_password, s_candidate_password, sizeof(s_saved_password));
        strlcpy(s_status.ssid, s_candidate_ssid, sizeof(s_status.ssid));
        s_status.candidate_pending = false;
        s_candidate_validation_started = false;
        s_status.has_saved_credentials = true;
        s_status.phase = WIFI_PHASE_SUCCESS;
    } else {
        strlcpy(s_status.ssid, s_saved_ssid, sizeof(s_status.ssid));
        s_status.phase = WIFI_PHASE_CONNECTED;
    }
    xSemaphoreGive(s_lock);

    if (candidate) {
        esp_timer_stop(s_candidate_timer);
        esp_err_t err = wifi_credentials_save(s_saved_ssid, s_saved_password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to persist Wi-Fi credentials: %s", esp_err_to_name(err));
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(s_status.error, "已联网，但凭据写入 NVS 失败", sizeof(s_status.error));
            xSemaphoreGive(s_lock);
        }
    }

    ESP_LOGI(TAG, "Wi-Fi connected: SSID=%s IP=%s", s_saved_ssid, ip);
    ESP_LOGI(TAG, "Open http://%s.local/ or http://%s/", CONFIG_WIFI_WEB_HOSTNAME, ip);
    esp_timer_stop(s_ap_shutdown_timer);
    esp_timer_start_once(s_ap_shutdown_timer, 5000000ULL);
    notify_changed();
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        handle_got_ip((const ip_event_got_ip_t *)event_data);
        return;
    }
    if (event_base != WIFI_EVENT || event_id != WIFI_EVENT_STA_DISCONNECTED) {
        return;
    }

    const wifi_event_sta_disconnected_t *event = event_data;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool pending = s_status.candidate_pending;
    bool has_saved = s_saved_ssid[0] != '\0';
    bool ap_active = s_status.ap_active;
    s_status.connected = false;
    s_status.ip[0] = '\0';
    if (!pending) {
        s_status.phase = ap_active ? WIFI_PHASE_PROVISIONING : WIFI_PHASE_CONNECTING;
        snprintf(s_status.error, sizeof(s_status.error), "Wi-Fi 已断开（原因 %u）", event->reason);
    }
    xSemaphoreGive(s_lock);

    if (!pending && has_saved) {
        schedule_reconnect();
        if (!ap_active && !esp_timer_is_active(s_fallback_timer)) {
            esp_timer_start_once(s_fallback_timer, (uint64_t)CONFIG_WIFI_WEB_FALLBACK_SECONDS * 1000000ULL);
        }
    }
    notify_changed();
}

static esp_err_t create_timer(const char *name, esp_timer_cb_t callback, esp_timer_handle_t *out)
{
    const esp_timer_create_args_t args = {
        .callback = callback,
        .name = name,
    };
    return esp_timer_create(&args, out);
}

// ---- Public component API ------------------------------------------------------

esp_err_t wifi_manager_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_netif_set_hostname(s_sta_netif, CONFIG_WIFI_WEB_HOSTNAME));

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

    ESP_ERROR_CHECK(create_timer("wifi_reconnect", reconnect_timer_cb, &s_reconnect_timer));
    ESP_ERROR_CHECK(create_timer("wifi_fallback", fallback_timer_cb, &s_fallback_timer));
    ESP_ERROR_CHECK(create_timer("wifi_candidate_start", candidate_start_timer_cb,
                                 &s_candidate_start_timer));
    ESP_ERROR_CHECK(create_timer("wifi_candidate", candidate_timer_cb, &s_candidate_timer));
    ESP_ERROR_CHECK(create_timer("wifi_ap_stop", ap_shutdown_timer_cb, &s_ap_shutdown_timer));

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_WIFI_WEB_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 Wi-Fi Web Template"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));

    bool has_saved = wifi_credentials_load(s_saved_ssid, sizeof(s_saved_ssid),
                                           s_saved_password, sizeof(s_saved_password)) == ESP_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.has_saved_credentials = has_saved;
    s_status.phase = has_saved ? WIFI_PHASE_CONNECTING : WIFI_PHASE_PROVISIONING;
    if (has_saved) {
        strlcpy(s_status.ssid, s_saved_ssid, sizeof(s_status.ssid));
    }
    xSemaphoreGive(s_lock);

    ESP_ERROR_CHECK(esp_wifi_set_mode(has_saved ? WIFI_MODE_STA : WIFI_MODE_APSTA));
    if (has_saved) {
        ESP_ERROR_CHECK(apply_station_config(s_saved_ssid, s_saved_password));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (has_saved) {
        ESP_ERROR_CHECK(esp_wifi_connect());
        ESP_ERROR_CHECK(esp_timer_start_once(s_fallback_timer,
                                             (uint64_t)CONFIG_WIFI_WEB_FALLBACK_SECONDS * 1000000ULL));
    } else {
        ESP_ERROR_CHECK(start_provisioning_ap());
    }
    return ESP_OK;
}

void wifi_manager_set_change_callback(wifi_manager_change_cb_t callback)
{
    s_change_callback = callback;
}

void wifi_manager_get_status(wifi_manager_status_t *status)
{
    if (status == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_lock);

    if (status->connected) {
        wifi_ap_record_t record;
        if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) {
            status->rssi = record.rssi;
        }
    }
}

const char *wifi_manager_phase_name(wifi_phase_t phase)
{
    switch (phase) {
    case WIFI_PHASE_CONNECTING: return "connecting";
    case WIFI_PHASE_PROVISIONING: return "provisioning";
    case WIFI_PHASE_TESTING: return "testing";
    case WIFI_PHASE_CONNECTED: return "connected";
    case WIFI_PHASE_SUCCESS: return "success";
    case WIFI_PHASE_FAILED: return "failed";
    default: return "idle";
    }
}

static wifi_security_t convert_auth_mode(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN: return WIFI_SECURITY_OPEN;
    case WIFI_AUTH_WEP: return WIFI_SECURITY_WEP;
    case WIFI_AUTH_WPA_PSK: return WIFI_SECURITY_WPA;
    case WIFI_AUTH_WPA2_PSK: return WIFI_SECURITY_WPA2;
    case WIFI_AUTH_WPA_WPA2_PSK: return WIFI_SECURITY_WPA_WPA2;
    case WIFI_AUTH_WPA3_PSK: return WIFI_SECURITY_WPA3;
    case WIFI_AUTH_WPA2_WPA3_PSK: return WIFI_SECURITY_WPA2_WPA3;
    default: return WIFI_SECURITY_OTHER;
    }
}

esp_err_t wifi_manager_scan(wifi_scan_result_t *results, uint16_t *count)
{
    if (results == NULL || count == NULL || *count == 0 ||
        *count > WIFI_MANAGER_MAX_SCAN_RESULTS) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t capacity = *count;
    wifi_ap_record_t *records = calloc(capacity, sizeof(*records));
    if (records == NULL) {
        return ESP_ERR_NO_MEM;
    }
    wifi_scan_config_t config = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&config, true);
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(&capacity, records);
    }
    if (err == ESP_OK) {
        for (uint16_t i = 0; i < capacity; ++i) {
            strlcpy(results[i].ssid, (const char *)records[i].ssid, sizeof(results[i].ssid));
            results[i].rssi = records[i].rssi;
            results[i].security = convert_auth_mode(records[i].authmode);
        }
        *count = capacity;
    }
    free(records);
    return err;
}

esp_err_t wifi_manager_submit_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);
    if (ssid_len == 0 || ssid_len > 32 || (pass_len != 0 && (pass_len < 8 || pass_len > 63))) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_status.candidate_pending) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    strlcpy(s_candidate_ssid, ssid, sizeof(s_candidate_ssid));
    strlcpy(s_candidate_password, password, sizeof(s_candidate_password));
    strlcpy(s_status.ssid, ssid, sizeof(s_status.ssid));
    s_status.candidate_pending = true;
    s_candidate_validation_started = false;
    s_status.connected = false;
    s_status.phase = WIFI_PHASE_TESTING;
    s_status.error[0] = '\0';
    s_status.ip[0] = '\0';
    xSemaphoreGive(s_lock);

    esp_timer_stop(s_reconnect_timer);
    esp_timer_stop(s_fallback_timer);
    esp_timer_stop(s_candidate_timer);
    esp_timer_stop(s_candidate_start_timer);
    esp_err_t err = esp_timer_start_once(s_candidate_start_timer, CANDIDATE_START_DELAY_US);
    if (err != ESP_OK) {
        restore_saved_after_candidate_failure("无法启动 Wi-Fi 连接");
        return err;
    }
    notify_changed();
    return ESP_OK;
}

esp_err_t wifi_manager_enter_provisioning(bool clear_saved)
{
    esp_timer_stop(s_reconnect_timer);
    esp_timer_stop(s_fallback_timer);
    esp_timer_stop(s_candidate_start_timer);
    esp_timer_stop(s_candidate_timer);
    esp_timer_stop(s_ap_shutdown_timer);

    if (clear_saved) {
        esp_err_t err = wifi_credentials_clear();
        if (err != ESP_OK) {
            return err;
        }
        memset(s_saved_ssid, 0, sizeof(s_saved_ssid));
        memset(s_saved_password, 0, sizeof(s_saved_password));
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.connected = false;
    s_status.candidate_pending = false;
    s_candidate_validation_started = false;
    s_status.has_saved_credentials = !clear_saved && s_saved_ssid[0] != '\0';
    s_status.phase = WIFI_PHASE_PROVISIONING;
    s_status.ip[0] = '\0';
    s_status.error[0] = '\0';
    if (clear_saved) {
        s_status.ssid[0] = '\0';
    }
    xSemaphoreGive(s_lock);

    esp_wifi_disconnect();
    esp_err_t err = start_provisioning_ap();
    notify_changed();
    return err;
}
