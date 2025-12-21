// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Scope Creep Labs LLC

#ifndef ACFA2A98_ED39_417E_A6C9_F1C489271BDA
#define ACFA2A98_ED39_417E_A6C9_F1C489271BDA

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"

#define STORAGE_NAMESPACE "storage"

esp_err_t init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t save_integer(const char* key, int32_t value) {
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_i32(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t get_integer(const char* key, int32_t* value, int32_t default_value) {
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_get_i32(handle, key, value);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_value;
        ret = ESP_OK;
    }
    nvs_close(handle);
    
    return ret;
}

#endif