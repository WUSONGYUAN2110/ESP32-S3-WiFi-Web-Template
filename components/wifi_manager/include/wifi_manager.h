#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_MANAGER_MAX_SCAN_RESULTS 20

/** Security classification independent of ESP-IDF's internal auth enum. */
typedef enum {
    WIFI_SECURITY_OPEN = 0,
    WIFI_SECURITY_WEP,
    WIFI_SECURITY_WPA,
    WIFI_SECURITY_WPA2,
    WIFI_SECURITY_WPA_WPA2,
    WIFI_SECURITY_WPA3,
    WIFI_SECURITY_WPA2_WPA3,
    WIFI_SECURITY_OTHER,
} wifi_security_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    wifi_security_t security;
} wifi_scan_result_t;

typedef enum {
    WIFI_PHASE_IDLE = 0,
    WIFI_PHASE_CONNECTING,
    WIFI_PHASE_PROVISIONING,
    WIFI_PHASE_TESTING,
    WIFI_PHASE_CONNECTED,
    WIFI_PHASE_SUCCESS,
    WIFI_PHASE_FAILED,
} wifi_phase_t;

typedef struct {
    wifi_phase_t phase;
    bool connected;
    bool ap_active;
    bool has_saved_credentials;
    bool candidate_pending;
    int8_t rssi;
    char ssid[33];
    char ip[16];
    char ap_ssid[33];
    char error[96];
} wifi_manager_status_t;

/** Called after an observable Wi-Fi state change; callback must not block. */
typedef void (*wifi_manager_change_cb_t)(void);

/** Initialize netifs, Wi-Fi, mDNS, timers and the provisioning state machine. */
esp_err_t wifi_manager_init(void);

/** Register one optional observer used to refresh application state. */
void wifi_manager_set_change_callback(wifi_manager_change_cb_t callback);

/** Copy a thread-safe status snapshot. Passwords are never exposed. */
void wifi_manager_get_status(wifi_manager_status_t *status);

/** Return the stable lowercase name serialized by the HTTP API. */
const char *wifi_manager_phase_name(wifi_phase_t phase);

/**
 * Perform a blocking active scan. On input, count is the result capacity;
 * on success, it is the number of records written.
 */
esp_err_t wifi_manager_scan(wifi_scan_result_t *results, uint16_t *count);

/** Test candidate credentials in RAM and persist only after DHCP succeeds. */
esp_err_t wifi_manager_submit_credentials(const char *ssid, const char *password);

/** Enter AP+STA provisioning, optionally deleting the saved credential pair. */
esp_err_t wifi_manager_enter_provisioning(bool clear_credentials);
