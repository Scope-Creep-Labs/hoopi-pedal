// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Scope Creep Labs LLC

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "cJSON.h"

// WiFi scan results
#define MAX_AP_NUM 20
static wifi_ap_record_t ap_records[MAX_AP_NUM];
static uint16_t ap_count = 0;
static bool scan_done = false;

wifi_config_t saved_wifi_config = {0};
bool wifi_sta_connected = false;

// Function prototypes
static esp_err_t wifi_scan_handler(httpd_req_t *req);
static void wifi_scan_task(void *pvParameters);

// API handler for WiFi scanning
static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    scan_done = false;
    
    // Create WiFi scan task
    xTaskCreate(wifi_scan_task, "wifi_scan_task", 4096, NULL, 5, NULL);
    
    // Wait for scan to complete
    while (!scan_done) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Build JSON response with scan results
    cJSON *root = cJSON_CreateArray();
    
    for (int i = 0; i < ap_count; i++) {
        if (strlen((char *)ap_records[i].ssid) == 0)
            continue;
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", ap_records[i].rssi);


        cJSON_AddNumberToObject(ap, "channel", ap_records[i].primary);
        
        // Determine frequency band based on channel
        const char *band = (ap_records[i].primary > 14) ? "5GHz" : "2.4GHz";
        cJSON_AddStringToObject(ap, "band", band);

        // Convert auth mode to string representation
        char *auth_mode;
        switch (ap_records[i].authmode)
        {
        case WIFI_AUTH_OPEN:
            auth_mode = "Open";
            break;
        case WIFI_AUTH_WEP:
            auth_mode = "WEP";
            break;
        case WIFI_AUTH_WPA_PSK:
            auth_mode = "WPA";
            break;
        case WIFI_AUTH_WPA2_PSK:
            auth_mode = "WPA2";
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            auth_mode = "WPA/WPA2";
            break;
        case WIFI_AUTH_WPA3_PSK:
            auth_mode = "WPA3";
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            auth_mode = "WPA2/WPA3";
            break;
        default:
            auth_mode = "Unknown";
            break;
        }

        cJSON_AddStringToObject(ap, "encryption", auth_mode);
        cJSON_AddItemToArray(root, ap);
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "ssids", root);
    if(wifi_sta_connected) {
        cJSON_AddStringToObject(response, "connectedTo", (char *) saved_wifi_config.sta.ssid);
    }

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

// Task for WiFi scanning
static void wifi_scan_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    // Check current WiFi mode
    wifi_mode_t current_mode;
    esp_err_t ret = esp_wifi_get_mode(&current_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(ret));
        scan_done = true;
        vTaskDelete(NULL);
        return;
    }
    
    // If we're in AP-only mode, temporarily switch to APSTA mode for scanning
    bool mode_changed = false;
    if (current_mode == WIFI_MODE_AP) {
        ESP_LOGI(TAG, "Switching from AP to APSTA mode for scanning");
        ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(ret));
            scan_done = true;
            vTaskDelete(NULL);
            return;
        }
        mode_changed = true;
        // Give some time for mode change to take effect
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Configure WiFi scan
    wifi_scan_config_t scan_config;
    memset(&scan_config, 0, sizeof(wifi_scan_config_t));
    scan_config.ssid = NULL;
    scan_config.bssid = NULL;
    scan_config.channel = 0;
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active.min = 100;
    scan_config.scan_time.active.max = 300;
    
    // Start scan
    ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        ap_count = 0;
    } else {
        // Get scan results
        ap_count = MAX_AP_NUM;
        ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
            ap_count = 0;
        } else {
            ESP_LOGI(TAG, "Scan completed, found %d access points", ap_count);
            
            for (int i = 0; i < ap_count; i++) {
                ESP_LOGI(TAG, "SSID: %s, RSSI: %d, Auth mode: %d", 
                         ap_records[i].ssid, ap_records[i].rssi, ap_records[i].authmode);
            }
        }
    }
    
    // If we changed the mode, switch back to AP-only mode
    if (mode_changed) {
        ESP_LOGI(TAG, "Switching back to AP-only mode");
        esp_wifi_set_mode(WIFI_MODE_AP);
    }
    
    scan_done = true;
    vTaskDelete(NULL);
}
