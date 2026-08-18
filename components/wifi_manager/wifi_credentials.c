#include "wifi_credentials.h"

#include <string.h>

#include "nvs.h"

#define WIFI_NAMESPACE "wifi_cfg"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PASS_KEY "pass"

esp_err_t wifi_credentials_load(char *ssid, size_t ssid_size,
                                char *password, size_t password_size)
{
    if (ssid == NULL || password == NULL || ssid_size == 0 || password_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ssid[0] = '\0';
    password[0] = '\0';

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t stored_ssid_size = ssid_size;
    size_t stored_password_size = password_size;
    err = nvs_get_str(handle, WIFI_SSID_KEY, ssid, &stored_ssid_size);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, WIFI_PASS_KEY, password, &stored_password_size);
    }
    nvs_close(handle);

    if (err != ESP_OK || ssid[0] == '\0') {
        memset(ssid, 0, ssid_size);
        memset(password, 0, password_size);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t wifi_credentials_save(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, WIFI_SSID_KEY, ssid);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, WIFI_PASS_KEY, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return err;
}

esp_err_t wifi_credentials_clear(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_erase_all(handle);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return err;
}
