// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Scope Creep Labs LLC

#ifndef F444CB5E_C703_4781_9351_D08DF8012A5E
#define F444CB5E_C703_4781_9351_D08DF8012A5E

#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_mac.h"
#include "dirent.h"
#include "cJSON.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"



#include "uart.h"
#include "wifi_utils.h"

// Embedded effects_config.json (from EMBED_TXTFILES in CMakeLists.txt)
extern const char effects_config_json_start[] asm("_binary_effects_config_json_start");
extern const char effects_config_json_end[] asm("_binary_effects_config_json_end");

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

#define WIFI_SSID      "Hoopi"
#define WIFI_PASS      "hoopirocks"
#define MAX_CLIENTS    4
#define MAX_RETRIES 3
static uint8_t s_retry_num = 0;

/* Scratch buffer size */
#define SCRATCH_BUFSIZE  512*1024  // 512KB for better performance (was 3.2MB) 

static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static esp_netif_t *ap_netif;
static esp_netif_t *sta_netif;

struct file_server_data {
    /* Base path of file storage */
    // char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

static httpd_handle_t server = NULL;

bool wifi_started = false;
bool wifi_busy = false; //skip udp broadcast when doing heavy API responses

static esp_err_t handle_list_files(httpd_req_t *req) {

    wifi_busy = true;

    if (!sd_ok) {
        ESP_LOGE(TAG, "SD card not mounted");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        wifi_busy = false;
        return ESP_FAIL;
    }

    auto json_file = fopen(SD_MOUNT_POINT "/recordings.json", "r");

    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;

    // ESP_LOGI(TAG, "Starting sending json file ...");

    httpd_resp_set_type(req, "application/octet-stream");
    
    do {
      chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, json_file);
      if (chunksize > 0)
      {
        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
        {
          fclose(json_file);
          ESP_LOGE(TAG, "File sending failed!");
          /* Abort sending file */
          httpd_resp_sendstr_chunk(req, NULL);
          /* Respond with 500 Internal Server Error */
          httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
          wifi_busy = false;
          return ESP_FAIL;
        }
      }
    } while (chunksize != 0);

    fclose(json_file);
    // ESP_LOGI(TAG, "File sending complete");

    httpd_resp_send_chunk(req, NULL, 0);

    wifi_busy = false;
    return ESP_OK;
}

int percent_decode(char* out, const char* in) {
    static const char tbl[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    char c, *beg = out;
    int8_t v1, v2;
    if (in != NULL)
    {
        while ((c = *in++) != '\0')
        {
            if (c == '%')
            {
                if ((v1 = tbl[(unsigned char)*in++]) < 0 ||
                    (v2 = tbl[(unsigned char)*in++]) < 0)
                {
                    *beg = '\0';
                    return -1;
                }
                c = (v1 << 4) | v2;
            }
            *out++ = c;
        }
    }
    *out = '\0';
    return 0;
}

static esp_err_t handle_wav_file(httpd_req_t *req) {
    if (!sd_ok) {
        ESP_LOGE(TAG, "SD card not mounted");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }

    char filepath[255+10];
    char decoded[255];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = req->uri; 


    // /* If name has trailing '/', respond with directory contents */
    // if (filename[strlen(filename) - 1] == '/') {
    //     return handle_list_files(req);
    // }

    filename = req->uri + 9; // Skip /api/wav/

    percent_decode(decoded, filename);
    ESP_LOGI(TAG, "Filename: %s %s", filename, decoded);

    snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, decoded);
    
    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fd = fopen(filepath, "rb");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read file: %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    wifi_busy = true;

    // Disable WiFi power save for maximum throughput during file transfer
    esp_wifi_set_ps(WIFI_PS_NONE);

    httpd_resp_set_type(req, "application/octet-stream");
    
    // Set Content-Length header to eliminate chunked encoding overhead
    char content_length_str[32];
    snprintf(content_length_str, sizeof(content_length_str), "%ld", file_stat.st_size);
    httpd_resp_set_hdr(req, "Content-Length", content_length_str);
    
    // Optimize TCP socket for file transfer
    int sock_fd = httpd_req_to_sockfd(req);
    if (sock_fd >= 0) {
        int nodelay = 1;
        setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        
        // Increase socket send buffer
        int sendbuf = 64 * 1024;  // 64KB
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf));
    }
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    
    do {
      chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
      if (chunksize > 0)
      {
        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
        {
          fclose(fd);
          ESP_LOGE(TAG, "File sending failed!");
          /* Abort sending file */
          httpd_resp_sendstr_chunk(req, NULL);
          /* Respond with 500 Internal Server Error */
          httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
          wifi_busy = false;
          return ESP_FAIL;
        }
      }
    } while (chunksize != 0);

    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    httpd_resp_send_chunk(req, NULL, 0);
    wifi_busy = false;
    return ESP_OK;
}

static esp_err_t handle_wav_file_resumable(httpd_req_t *req) {
    if (!sd_ok) {
        ESP_LOGE(TAG, "SD card not mounted");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }

    char filepath[255+10];
    char decoded[255];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = req->uri + 18; // Skip /api/wav/resumable/

    percent_decode(decoded, filename);
    ESP_LOGI(TAG, "Resumable download: %s %s", filename, decoded);

    snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, decoded);

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fd = fopen(filepath, "rb");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read file: %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Parse Range header
    size_t range_start = 0;
    size_t range_end = file_stat.st_size - 1;
    bool is_range_request = false;

    char range_header[64] = {0};
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Range");
    if (hdr_len > 0 && hdr_len < sizeof(range_header)) {
        httpd_req_get_hdr_value_str(req, "Range", range_header, sizeof(range_header));

        // Parse "bytes=start-end" or "bytes=start-"
        if (strncmp(range_header, "bytes=", 6) == 0) {
            char *range_spec = range_header + 6;
            char *dash = strchr(range_spec, '-');

            if (dash) {
                *dash = '\0';
                range_start = atol(range_spec);

                if (*(dash + 1) != '\0') {
                    range_end = atol(dash + 1);
                }

                // Validate range
                if (range_start >= file_stat.st_size || range_end >= file_stat.st_size || range_start > range_end) {
                    fclose(fd);
                    httpd_resp_set_status(req, "416 Range Not Satisfiable");
                    char content_range[64];
                    snprintf(content_range, sizeof(content_range), "bytes */%ld", file_stat.st_size);
                    httpd_resp_set_hdr(req, "Content-Range", content_range);
                    httpd_resp_send(req, NULL, 0);
                    return ESP_FAIL;
                }

                is_range_request = true;
                ESP_LOGI(TAG, "Range request: bytes %zu-%zu/%ld", range_start, range_end, file_stat.st_size);
            }
        }
    }

    ESP_LOGI(TAG, "Sending file: %s (%zu-%zu of %ld bytes)...", filename, range_start, range_end, file_stat.st_size);
    wifi_busy = true;

    // Disable WiFi power save for maximum throughput during file transfer
    esp_wifi_set_ps(WIFI_PS_NONE);

    httpd_resp_set_type(req, "application/octet-stream");

    // Set Accept-Ranges header to indicate range support
    httpd_resp_set_hdr(req, "Accept-Ranges", "bytes");

    size_t content_length = range_end - range_start + 1;

    if (is_range_request) {
        // Set status to 206 Partial Content
        httpd_resp_set_status(req, "206 Partial Content");

        // Set Content-Range header
        char content_range[64];
        snprintf(content_range, sizeof(content_range), "bytes %zu-%zu/%ld", range_start, range_end, file_stat.st_size);
        httpd_resp_set_hdr(req, "Content-Range", content_range);
    }

    // Set Content-Length header
    char content_length_str[32];
    snprintf(content_length_str, sizeof(content_length_str), "%zu", content_length);
    httpd_resp_set_hdr(req, "Content-Length", content_length_str);

    // Optimize TCP socket for file transfer
    int sock_fd = httpd_req_to_sockfd(req);
    if (sock_fd >= 0) {
        int nodelay = 1;
        setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        // Increase socket send buffer
        int sendbuf = 64 * 1024;  // 64KB
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf));
    }

    // Seek to start position if this is a range request
    if (range_start > 0) {
        if (fseek(fd, range_start, SEEK_SET) != 0) {
            fclose(fd);
            ESP_LOGE(TAG, "Failed to seek to position %zu", range_start);
            httpd_resp_send_500(req);
            wifi_busy = false;
            return ESP_FAIL;
        }
    }

    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t bytes_remaining = content_length;
    size_t chunksize;

    do {
        size_t to_read = (bytes_remaining < SCRATCH_BUFSIZE) ? bytes_remaining : SCRATCH_BUFSIZE;
        chunksize = fread(chunk, 1, to_read, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                wifi_busy = false;
                return ESP_FAIL;
            }
            bytes_remaining -= chunksize;
        }
    } while (chunksize != 0 && bytes_remaining > 0);

    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    httpd_resp_send_chunk(req, NULL, 0);
    wifi_busy = false;
    return ESP_OK;
}

static esp_err_t handle_delete_file(httpd_req_t *req) {
    if (!sd_ok) {
        ESP_LOGE(TAG, "SD card not mounted");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }

    char filepath[FILE_PATH_MAX];
    char decoded[FILE_PATH_MAX-10];

    const char *filename = req->uri + strlen("/api/delete/");

    // Basic security check to prevent directory traversal
    if (strstr(filename, "..") != NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    // Check file extension
    const char *ext = strrchr(filename, '.');
    if (!ext || strcasecmp(ext, ".wav") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Only .wav files can be deleted");
        return ESP_FAIL;
    }

    percent_decode(decoded, filename);

    snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, decoded);

    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    // Delete the file
    if (unlink(filepath) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
        return ESP_FAIL;
    }

    // Create success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "File deleted successfully");
    cJSON_AddStringToObject(response, "deleted_file", filename);

    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);

    generate_wav_json();
    return ESP_OK;
}

static esp_err_t handle_format_sdcard(httpd_req_t *req) {
    ESP_LOGI(TAG, "SD card format requested");
    
    // Create response JSON
    cJSON *response = cJSON_CreateObject();
    
    // Call the format function from sdcard.h
    bool format_success = format_sdcard();
    
    if (format_success) {
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "SD card formatted successfully");
        ESP_LOGI(TAG, "SD card format completed successfully");
    } else {
        cJSON_AddStringToObject(response, "status", "error");
        if (is_recording) {
            cJSON_AddStringToObject(response, "message", "Cannot format SD card while recording is in progress");
        } else if (card == NULL) {
            cJSON_AddStringToObject(response, "message", "SD card not mounted");
        } else {
            cJSON_AddStringToObject(response, "message", "Failed to format SD card");
        }
        ESP_LOGE(TAG, "SD card format failed");
    }
    
    // Send JSON response
    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

static esp_err_t handle_seed_dfu(httpd_req_t *req) {
    ESP_LOGI(TAG, "Daisy Seed DFU mode requested");
    
    // Create response JSON
    cJSON *response = cJSON_CreateObject();
    
    // Reset seed_dfu flag before sending command
    seed_dfu = false;
    
    // Send DFU command to seed
    send_dfu_cmd();
    
    const int max_uart_retries = 10;
    int retry_count = 0;
    
    // Wait for seed_dfu to be set to true (similar to record_event_handler pattern)
    while (!seed_dfu) {
        send_dfu_cmd();
        
        retry_count++;
        if (retry_count == max_uart_retries) {
            cJSON_AddStringToObject(response, "status", "error");
            cJSON_AddStringToObject(response, "message", "Unable to communicate with Daisy via UART");
            ESP_LOGE(TAG, "Failed to switch Daisy Seed to DFU mode - UART timeout");
            
            // Send JSON response
            char *json_str = cJSON_PrintUnformatted(response);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, json_str);
            
            free(json_str);
            cJSON_Delete(response);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    // Success - seed_dfu is now true
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Daisy Seed switched to DFU mode successfully");
    ESP_LOGI(TAG, "Daisy Seed DFU mode activated successfully");
    
    // Send JSON response
    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}


static esp_err_t save_wifi_config(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t config_handler(httpd_req_t *req)
{
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        return ESP_FAIL;
    }

    char ssid[32] = {0};
    char password[64] = {0};
    
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *ssid_json = cJSON_GetObjectItem(root, "ssid");
        cJSON *pass_json = cJSON_GetObjectItem(root, "pass");
        if (ssid_json && pass_json) {
            strncpy(ssid, ssid_json->valuestring, sizeof(ssid) - 1);
            strncpy(password, pass_json->valuestring, sizeof(password) - 1);
        }
        cJSON_Delete(root);
    }

    if (strlen(ssid) && strlen(password)) {
        save_wifi_config(ssid, password);

        // Check current WiFi mode
        wifi_mode_t current_mode;
        esp_wifi_get_mode(&current_mode);

        wifi_config_t wifi_config = {0};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

        s_retry_num = 0;
        
        // If current mode is AP, switch to APSTA mode without stopping WiFi
        if (current_mode == WIFI_MODE_AP) {
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        } else {
            ESP_ERROR_CHECK(esp_wifi_stop());
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_start());
        }
        
        ESP_ERROR_CHECK(esp_wifi_connect());

        // Wait for connection result with timeout (5 seconds)
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                              CONNECTED_BIT,
                                              pdFALSE,
                                              pdFALSE,
                                              5000 / portTICK_PERIOD_MS);

        if (bits & CONNECTED_BIT) {
            const char resp[] = "{\"status\":\"ok\"}";
            httpd_resp_send(req, resp, strlen(resp));
            return ESP_OK;
        } else {
            const char resp[] = "{\"status\":\"error\"}";
            httpd_resp_send(req, resp, strlen(resp));
            return ESP_OK;
        }
    }

    const char resp[] = "{\"status\":\"error\"}";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t load_wifi_config(wifi_config_t* wifi_config) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;

    size_t ssid_len = sizeof(wifi_config->sta.ssid);
    size_t pass_len = sizeof(wifi_config->sta.password);

    err = nvs_get_str(nvs_handle, "ssid", (char*)wifi_config->sta.ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, "password", (char*)wifi_config->sta.password, &pass_len);
    nvs_close(nvs_handle);
    return err;
}
char ap_ssid[32];
static void switch_to_ap_mode(void)
{
    
    ESP_LOGI(TAG, "Starting AP mode with SSID: %s", ap_ssid);

    // Stop WiFi first
    ESP_ERROR_CHECK(esp_wifi_stop());
    
    // Set country configuration before mode
    wifi_country_t country = {
        .cc = "US",
        .schan = 1,
        .nchan = 11,
        .max_tx_power = 84,  // 21 dBm
        .policy = WIFI_COUNTRY_POLICY_MANUAL
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
    
    // Configure AP
    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.ap.ssid, ap_ssid);
    strcpy((char *)wifi_config.ap.password, WIFI_PASS);
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    wifi_config.ap.max_connection = MAX_CLIENTS;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.channel = 1;  // Use channel 1 for better compatibility
    wifi_config.ap.ssid_hidden = 0;  // Make sure SSID is not hidden
    wifi_config.ap.beacon_interval = 100;  // Standard beacon interval
    wifi_config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;  // Force CCMP cipher for WPA2
    
    // If password is too short for WPA2, fall back to open
    if (strlen(WIFI_PASS) < 8) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_LOGW(TAG, "Password too short for WPA2, using open network");
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    // Disable power save mode
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    
    // Set protocol to support all modes
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    
    // Configure AP IP address
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    esp_netif_ip_info_t ip_info = {
        .ip = { .addr = ESP_IP4TOADDR(192, 168, 4, 1) },
        .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },
        .gw = { .addr = ESP_IP4TOADDR(192, 168, 4, 1) }
    };
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Set maximum TX power after WiFi is started
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84));  // 21 dBm
    
    // Log configuration
    int8_t max_power;
    esp_wifi_get_max_tx_power(&max_power);
    ESP_LOGI(TAG, "AP started. SSID: %s, Password: %s, Channel: %d, Max TX Power: %d", 
             ap_ssid, WIFI_PASS, wifi_config.ap.channel, max_power);
    
    // Verify configuration
    wifi_config_t check_config;
    esp_wifi_get_config(WIFI_IF_AP, &check_config);
    ESP_LOGI(TAG, "AP Config Check - SSID: %s, Channel: %d, Auth: %d, Hidden: %d", 
             (char*)check_config.ap.ssid, check_config.ap.channel, 
             check_config.ap.authmode, check_config.ap.ssid_hidden);
}

static void try_connect_saved_wifi(void) {
    esp_err_t err = load_wifi_config(&saved_wifi_config);

    // strcpy((char *)saved_wifi_config.sta.ssid, "rotikapda2025");
    // strcpy((char *)saved_wifi_config.sta.password, "XXXX");
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Connecting to saved network: %s", saved_wifi_config.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &saved_wifi_config));

        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // Disable power save mode

        // Set WiFi country info for maximum channel support
        wifi_country_t country = {
            .cc = "US",         // Country code (US has most permissive channels)
            .schan = 1,         // Start channel
            .nchan = 13,        // Number of channels
            .max_tx_power = 84, // Maximum transmit power (in 0.25 dBm units)
            .policy = WIFI_COUNTRY_POLICY_AUTO};
        ESP_ERROR_CHECK(esp_wifi_set_country(&country));

        // Set to highest data rate possible (must be supported by both AP and STA)
        ESP_ERROR_CHECK(esp_wifi_config_80211_tx_rate(WIFI_IF_STA, WIFI_PHY_RATE_MCS7_SGI));

        // Set WiFi protocol to allow maximum compatibility and performance
        ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA,
                                              WIFI_PROTOCOL_11B |
                                                  WIFI_PROTOCOL_11G |
                                                  WIFI_PROTOCOL_11N |
                                                  WIFI_PROTOCOL_LR));

        // Set bandwidth to maximum (40MHz) for higher throughput
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40));

        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else {
        ESP_LOGI(TAG, "No saved credentials, starting AP mode");
        switch_to_ap_mode();
    }
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " connected to AP", MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " disconnected from AP", MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP Started successfully");
        wifi_started = true;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRIES) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            wifi_sta_connected = false;
            ESP_LOGI(TAG, "Failed to connect to saved WiFi, switching to AP mode");
            switch_to_ap_mode();
        }
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_sta_connected = true;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        wifi_started = true;
    }
}

static esp_err_t record_event_handler(httpd_req_t *req) {
    char buf[266];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        return ESP_FAIL;
    }

    char startstop[10] = {0};
    time_t client_timestamp = 0;

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *filename_json = cJSON_GetObjectItem(root, "filename");
        cJSON *startstop_json = cJSON_GetObjectItem(root, "startstop");
        cJSON *timestamp_json = cJSON_GetObjectItem(root, "timestamp");

        if (filename_json && startstop_json) {
            strncpy(new_filename, filename_json->valuestring, sizeof(new_filename) - 1);
            strncpy(startstop, startstop_json->valuestring, sizeof(startstop) - 1);
        }

        if (timestamp_json) {
            client_timestamp = (time_t)timestamp_json->valueint;
        }

        cJSON_Delete(root);
    }

    const int max_uart_retries = 10;
    int retry_count = 0;

    if (strlen(new_filename) && strlen(startstop)) {
        if(strcmp(startstop, "start") == 0) {
            if (!sd_ok) {
                const char resp[] = "{\"status\":\"Error: SD card not mounted.\"}";
                httpd_resp_send(req, resp, strlen(resp));
                return ESP_OK;
            }

            // Check if system time needs to be set
            if (client_timestamp > 0) {
                time_t current_time;
                time(&current_time);

                // Check if system time is before 1980 (Jan 1, 1980 = 315532800)
                if (current_time < 315532800) {
                    struct timeval tv = {
                        .tv_sec = client_timestamp,
                        .tv_usec = 0
                    };

                    if (settimeofday(&tv, NULL) == 0) {
                        ESP_LOGI(TAG, "System time set from client timestamp: %lld", (long long)client_timestamp);
                    } else {
                        ESP_LOGE(TAG, "Failed to set system time");
                    }
                }
            }

            if (is_recording) {
                const char resp[] = "{\"status\":\"Error: A recording is already in progress.\"}";
                httpd_resp_send(req, resp, strlen(resp));
                return ESP_OK;
            }

            user_specified_filename = true;
            while (!is_recording) 
            {
                send_start_rec_cmd();

                retry_count++;
                if (retry_count == max_uart_retries)
                {
                    const char resp[] = "{\"status\":\"Error: Unable to communicate with Daisy via UART.\"}";
                    httpd_resp_send(req, resp, strlen(resp));

                    user_specified_filename = false;
                    return ESP_OK;
                }
                vTaskDelay(pdMS_TO_TICKS(200));
                
            }

        }
        else {
            if (!sd_ok) {
                const char resp[] = "{\"status\":\"Error: SD card not mounted.\"}";
                httpd_resp_send(req, resp, strlen(resp));
                return ESP_OK;
            }

            if (!is_recording) {
                const char resp[] = "{\"status\":\"Error: No recording in progress.\"}";
                httpd_resp_send(req, resp, strlen(resp));
                return ESP_OK;
            }
            // if (strcmp(new_filename, filename) != 0)
            // {
            //     const char resp[] = "{\"status\":\"Error: Filename mismatch.\"}";
            //     httpd_resp_send(req, resp, strlen(resp));
            //     return ESP_OK;
            // }

            while (is_recording) 
            {
                send_stop_rec_cmd();

                retry_count++;
                if (retry_count == max_uart_retries)
                {
                    const char resp[] = "{\"status\":\"Error: Unable to communicate with Daisy via UART.\"}";
                    httpd_resp_send(req, resp, strlen(resp));
                    return ESP_OK;
                }
                vTaskDelay(pdMS_TO_TICKS(200));

            }

        }

        char resp[400];
        int duration_seconds = written_bytes / BYTE_RATE;
        sprintf(resp, "{\"status\":\"ok\", \"filename\": \"%s\", \"duration_sec\": %d, \"size_bytes\": %zu, \"error_count\": %d}", filename, duration_seconds, written_bytes, error_count);
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }

    const char resp[] = "{\"status\":\"error\"}";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static void get_sd_card_info(cJSON *sdcard_json) {
    if (card == NULL || !sd_ok) {
        cJSON_AddBoolToObject(sdcard_json, "mounted", false);
        cJSON_AddNumberToObject(sdcard_json, "total_bytes", 0);
        cJSON_AddNumberToObject(sdcard_json, "free_bytes", 0);
        cJSON_AddNumberToObject(sdcard_json, "used_bytes", 0);
        return;
    }

    FATFS *fs;
    DWORD fre_clust, fre_sect, tot_sect;
    uint64_t total_bytes = 0, free_bytes = 0, used_bytes = 0;

    // Get volume information and free clusters
    FRESULT res = f_getfree(mount_point, &fre_clust, &fs);

    if (res == FR_OK) {
        tot_sect = (fs->n_fatent - 2) * fs->csize;
        fre_sect = fre_clust * fs->csize;

        total_bytes = (uint64_t) tot_sect * fs->ssize;
        free_bytes = (uint64_t) fre_sect * fs->ssize;
        used_bytes = total_bytes - free_bytes;

        cJSON_AddBoolToObject(sdcard_json, "mounted", true);
        cJSON_AddNumberToObject(sdcard_json, "total_bytes", total_bytes);
        cJSON_AddNumberToObject(sdcard_json, "free_bytes", free_bytes);
        cJSON_AddNumberToObject(sdcard_json, "used_bytes", used_bytes);
    } else {
        // f_getfree failed, report as not mounted
        cJSON_AddBoolToObject(sdcard_json, "mounted", false);
        cJSON_AddNumberToObject(sdcard_json, "total_bytes", 0);
        cJSON_AddNumberToObject(sdcard_json, "free_bytes", 0);
        cJSON_AddNumberToObject(sdcard_json, "used_bytes", 0);
    }
}


static esp_err_t status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    // WiFi Status section
    cJSON *wifi_json = cJSON_CreateObject();
    
    if(wifi_sta_connected) {
        cJSON_AddStringToObject(wifi_json, "ssid", (char *) saved_wifi_config.sta.ssid);
        cJSON_AddBoolToObject(wifi_json, "connected", true);
    } else {
        cJSON_AddBoolToObject(wifi_json, "connected", false);
        cJSON_AddStringToObject(wifi_json, "ssid", "");
    }

    cJSON *effect_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(effect_json, "programId", programId);

    cJSON_AddNumberToObject(effect_json, "controlNumber1", controlNumber1);
    cJSON_AddNumberToObject(effect_json, "controlValue1", controlValue1);

    cJSON_AddNumberToObject(effect_json, "controlNumber2", controlNumber2);
    cJSON_AddNumberToObject(effect_json, "controlValue2", controlValue2);

    cJSON_AddNumberToObject(effect_json, "controlNumber3", controlNumber3);
    cJSON_AddNumberToObject(effect_json, "controlValue3", controlValue3);

    cJSON_AddNumberToObject(effect_json, "controlNumber4", controlNumber4);
    cJSON_AddNumberToObject(effect_json, "controlValue4", controlValue4);
    
    // SD Card Status section
    cJSON *sdcard_json = cJSON_CreateObject();
    get_sd_card_info(sdcard_json);
    
    // Add sections to the root object
    cJSON_AddItemToObject(root, "wifi", wifi_json);
    cJSON_AddItemToObject(root, "sdcard", sdcard_json);
    cJSON_AddItemToObject(root, "effect", effect_json);
    
    // System Information section
    cJSON *system_json = cJSON_CreateObject();
    
    // ESP32 chip information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    cJSON_AddStringToObject(system_json, "model", "ESP32");
    cJSON_AddNumberToObject(system_json, "cores", chip_info.cores);
    cJSON_AddNumberToObject(system_json, "revision", chip_info.revision);
    
    // Memory information
    cJSON_AddNumberToObject(system_json, "heap_size", esp_get_free_heap_size() + esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(system_json, "free_heap", esp_get_free_heap_size());
    
    // Add system info to the root object
    cJSON_AddItemToObject(root, "system", system_json);
    
    // Firmware versions at root level
    cJSON_AddNumberToObject(root, "seed_fw_version", seed_fw_version);
    cJSON_AddNumberToObject(root, "esp32_fw_version", esp32_fw_version);
    
    // Convert to string and send response
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

static esp_err_t program_change_event_handler(httpd_req_t *req) {
    char buf[266];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0)
    {
        const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }

    uint8_t newProgramId;

    cJSON *root = cJSON_Parse(buf);
    if (root)
    {
        cJSON *pId_json = cJSON_GetObjectItem(root, "programId");
        if (pId_json)
        {
            newProgramId = pId_json->valueint;
        }
        else
        {
            const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
            httpd_resp_send(req, resp, strlen(resp));
            cJSON_Delete(root);
            return ESP_OK;
        }
        cJSON_Delete(root);
    }
    else
    {
        const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
        httpd_resp_send(req, resp, strlen(resp));
        cJSON_Delete(root);
        return ESP_OK;
    }

    const int max_uart_retries = 10;
    int retry_count = 0;
    clearRxBuffer();
    while (uart_recv_buffer[UART_PROGRAM_CHANGE_INDEX] != newProgramId)
    {
        send_program_change_cmd(newProgramId, true);

        retry_count++;
        if (retry_count == max_uart_retries)
        {
            const char resp[] = "{\"status\":\"Error: Unable to communicate with Daisy via UART.\"}";
            httpd_resp_send(req, resp, strlen(resp));

            user_specified_filename = false;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    char resp[363];
    sprintf(resp, "{\"status\":\"ok\", \"programId\": \"%d\"}", newProgramId);
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t effect_change_event_handler(httpd_req_t *req) {
    char buf[266];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0)
    {
        const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }

    uint8_t newProgramId;
    uint8_t controlNumber;
    uint8_t controlValue;

    cJSON *root = cJSON_Parse(buf);
    if (root)
    {
        cJSON *pId_json = cJSON_GetObjectItem(root, "programId");
        cJSON *cId_json = cJSON_GetObjectItem(root, "controlNumber");
        cJSON *value_json = cJSON_GetObjectItem(root, "value");
        if (pId_json && cId_json && value_json)
        {
            newProgramId = pId_json->valueint;
            controlNumber = cId_json->valueint;
            controlValue = value_json->valueint;
        }
        else
        {
            const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
            httpd_resp_send(req, resp, strlen(resp));
            cJSON_Delete(root);
            return ESP_OK;
        }
        cJSON_Delete(root);
    }
    else
    {
        const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
        httpd_resp_send(req, resp, strlen(resp));
        cJSON_Delete(root);
        return ESP_OK;
    }

    const int max_uart_retries = 10;
    int retry_count = 0;
    while (uart_recv_buffer[UART_PROGRAM_CHANGE_INDEX] != newProgramId)
    {
        send_program_change_cmd(newProgramId, newProgramId != programId); //clear CC slots if changing to a new effect

        retry_count++;
        if (retry_count == max_uart_retries)
        {
            const char resp[] = "{\"status\":\"Error: Unable to communicate with Daisy via UART.\"}";
            httpd_resp_send(req, resp, strlen(resp));

            user_specified_filename = false;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    retry_count = 0;
    while (uart_recv_buffer[UART_CONTROL_CHANGE_INDEX] != controlNumber || uart_recv_buffer[UART_CONTROL_CHANGE_INDEX+1] != controlValue )
    {
        send_control_change_cmd(controlNumber, controlValue);

        retry_count++;
        if (retry_count == max_uart_retries)
        {
            const char resp[] = "{\"status\":\"Error: Unable to communicate with Daisy via UART.\"}";
            httpd_resp_send(req, resp, strlen(resp));

            user_specified_filename = false;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    char resp[363];
    sprintf(resp, "{\"status\":\"ok\", \"programId\": \"%d\", \"controlNumber\": \"%d\", \"controlValue\": \"%d\"}", newProgramId, controlNumber, controlValue);
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t control_change_event_handler(httpd_req_t *req) {
    char buf[266];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0)
    {
        const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }

    uint8_t controlId;
    uint8_t value;

    cJSON *root = cJSON_Parse(buf);
    if (root)
    {
        cJSON *cId_json = cJSON_GetObjectItem(root, "controlId");
        cJSON *vId_json = cJSON_GetObjectItem(root, "value");
        if (cId_json && vId_json)
        {
            controlId = cId_json->valueint;
            value = vId_json->valueint;
        }
        else
        {
            const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
            httpd_resp_send(req, resp, strlen(resp));
            cJSON_Delete(root);
            return ESP_OK;
        }
        cJSON_Delete(root);
    }
    else
    {
        const char resp[] = "{\"status\":\"Error: Unable to parse JSON.\"}";
        httpd_resp_send(req, resp, strlen(resp));
        cJSON_Delete(root);
        return ESP_OK;
    }

    const int max_uart_retries = 10;
    int retry_count = 0;
    while (uart_recv_buffer[UART_CONTROL_CHANGE_INDEX] != controlId || uart_recv_buffer[UART_CONTROL_CHANGE_INDEX+1] != value )
    {
        send_control_change_cmd(controlId, value);

        retry_count++;
        if (retry_count == max_uart_retries)
        {
            const char resp[] = "{\"status\":\"Error: Unable to communicate with Daisy via UART.\"}";
            httpd_resp_send(req, resp, strlen(resp));

            user_specified_filename = false;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    char resp[363];
    sprintf(resp, "{\"status\":\"ok\", \"controlId\": \"%d\", \"value\": \"%d\"}", controlId, value);
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// Set parameter endpoint - uses new cmd=8 protocol
// JSON body: { "effect_idx": 0-7, "param_id": 0-255, "value": 0-255, "extra": 0-255 (optional) }
static esp_err_t set_parameter_handler(httpd_req_t *req) {
    char buf[266];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unable to read request body\"}");
        return ESP_OK;
    }
    buf[ret] = '\0';

    uint8_t effect_idx = 0;
    uint8_t param_id = 0;
    uint8_t value = 0;
    uint8_t extra = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *effect_json = cJSON_GetObjectItem(root, "effect_idx");
    cJSON *param_json = cJSON_GetObjectItem(root, "param_id");
    cJSON *value_json = cJSON_GetObjectItem(root, "value");
    cJSON *extra_json = cJSON_GetObjectItem(root, "extra");

    if (!param_json || !value_json) {
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Missing required fields: param_id, value\"}");
        return ESP_OK;
    }

    if (effect_json) effect_idx = (uint8_t)effect_json->valueint;
    param_id = (uint8_t)param_json->valueint;
    value = (uint8_t)value_json->valueint;
    if (extra_json) extra = (uint8_t)extra_json->valueint;

    cJSON_Delete(root);

    // Validate effect_idx
    if (effect_idx > 7) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"effect_idx must be 0-7\"}");
        return ESP_OK;
    }

    // Validate param is settable via UART (global params or uart_only effect params)
    if (!is_param_settable(effect_idx, param_id)) {
        char err_resp[150];
        snprintf(err_resp, sizeof(err_resp),
            "{\"status\":\"error\",\"message\":\"param_id %d is not a UART-settable parameter for effect %d (knob params cannot be set via API)\"}",
            param_id, effect_idx);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, err_resp);
        return ESP_OK;
    }

    // Send command with retry logic
    const int max_retries = 5;
    const int ack_timeout_ms = 1000;
    bool ack_received = false;

    for (int retry = 0; retry < max_retries; retry++) {
        param_ack_received = false;
        send_set_parameter_cmd(effect_idx, param_id, value, extra);

        // Wait for ACK with timeout
        int wait_ms = 0;
        while (!param_ack_received && wait_ms < ack_timeout_ms) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_ms += 50;
        }

        if (param_ack_received) {
            ack_received = true;
            break;
        }
        ESP_LOGW("HOOPI", "API setparam ACK timeout, retry %d/%d", retry + 1, max_retries);
    }

    if (!ack_received) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"UART timeout waiting for ACK after retries\"}");
        return ESP_OK;
    }

    // Verify the ACK matches what we sent
    if (last_param_ack_effect != effect_idx ||
        last_param_ack_param != param_id) {
        httpd_resp_set_type(req, "application/json");
        char resp[200];
        snprintf(resp, sizeof(resp),
            "{\"status\":\"error\",\"message\":\"ACK mismatch\",\"expected\":{\"effect\":%d,\"param\":%d},\"received\":{\"effect\":%d,\"param\":%d}}",
            effect_idx, param_id, last_param_ack_effect, last_param_ack_param);
        httpd_resp_sendstr(req, resp);
        return ESP_OK;
    }

    // Persist global parameters (param_id 0-12, 30-36) to NVS
    if (param_id <= 12 || (param_id >= 30 && param_id <= 36)) {
        switch (param_id) {
            case 0:  global_blend_mode = last_param_ack_value; global_blend_apply_to_rec = extra; break;
            case 1:  global_galaxylite_damping = last_param_ack_value; break;
            case 2:  global_galaxylite_predelay = last_param_ack_value; break;
            case 3:  global_galaxylite_mix = last_param_ack_value; break;
            case 4:  global_comp_threshold = last_param_ack_value; break;
            case 5:  global_comp_ratio = last_param_ack_value; break;
            case 6:  global_comp_attack = last_param_ack_value; break;
            case 7:  global_comp_release = last_param_ack_value; break;
            case 8:  global_comp_makeup = last_param_ack_value; break;
            case 9:  global_gate_threshold = last_param_ack_value; break;
            case 10: global_gate_attack = last_param_ack_value; break;
            case 11: global_gate_hold = last_param_ack_value; break;
            case 12: global_gate_release = last_param_ack_value; break;
            // EQ params (30-36)
            case 30: global_eq_enable = last_param_ack_value; break;
            case 31: global_eq_low_gain = last_param_ack_value; break;
            case 32: global_eq_mid_gain = last_param_ack_value; break;
            case 33: global_eq_high_gain = last_param_ack_value; break;
            case 34: global_eq_low_freq = last_param_ack_value; break;
            case 35: global_eq_mid_freq = last_param_ack_value; break;
            case 36: global_eq_high_freq = last_param_ack_value; break;
        }
        save_program_config();
        ESP_LOGI(TAG, "Global param %d persisted to NVS: %d", param_id, last_param_ack_value);
    }
    // Persist effect-specific parameters (param_id >= 14) to NVS
    else if (param_id >= EFFECT_PARAM_BASE && param_id < EFFECT_PARAM_BASE + EFFECT_PARAM_COUNT && effect_idx < 8) {
        effect_params[effect_idx][param_id - EFFECT_PARAM_BASE] = last_param_ack_value;
        save_program_config();
        ESP_LOGI(TAG, "Effect %d param %d persisted to NVS: %d", effect_idx, param_id, last_param_ack_value);
    }

    // Success response
    char resp[200];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"effect_idx\":%d,\"param_id\":%d,\"value\":%d,\"confirmed_value\":%d}",
        effect_idx, param_id, value, last_param_ack_value);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// Select effect endpoint - uses new cmd=255 protocol
// JSON body: { "effect_id": 0-7 }
static esp_err_t select_effect_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unable to read request body\"}");
        return ESP_OK;
    }
    buf[ret] = '\0';

    uint8_t effect_id = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *effect_json = cJSON_GetObjectItem(root, "effect_id");
    if (!effect_json) {
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Missing required field: effect_id\"}");
        return ESP_OK;
    }

    effect_id = (uint8_t)effect_json->valueint;
    cJSON_Delete(root);

    // Validate effect_id range
    if (effect_id > 7) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"effect_id must be 0-7\"}");
        return ESP_OK;
    }

    // Send command with retry logic using effect_ack_received flag
    const int max_retries = 5;
    const int ack_timeout_ms = 1000;
    bool ack_received = false;

    for (int retry = 0; retry < max_retries; retry++) {
        effect_ack_received = false;
        send_select_effect_cmd(effect_id);

        // Wait for ACK (cmd=7) with timeout
        int wait_ms = 0;
        while (!effect_ack_received && wait_ms < ack_timeout_ms) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_ms += 50;
        }

        if (effect_ack_received && programId == effect_id) {
            ack_received = true;
            break;
        }
        ESP_LOGW("HOOPI", "API selecteffect ACK timeout, retry %d/%d", retry + 1, max_retries);
    }

    if (!ack_received) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"UART timeout waiting for effect switch after retries\"}");
        return ESP_OK;
    }

    // Success response
    char resp[100];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"effect_id\":%d}", effect_id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// Serve embedded effects_config.json
static esp_err_t effects_config_handler(httpd_req_t *req) {
    // Use strlen since EMBED_TXTFILES adds null terminator
    size_t config_len = strlen(effects_config_json_start);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, effects_config_json_start, config_len);
    return ESP_OK;
}

// Get current parameter values (global + effect-specific uart_only params + knobs)
static esp_err_t get_params_handler(httpd_req_t *req) {
    // Request knob values from Daisy and wait for response
    send_request_knob_values();

    // Wait for knob values response (max 1000ms)
    int wait_count = 0;
    while (!knob_values_received && wait_count < 100) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_count++;
    }

    cJSON *root = cJSON_CreateObject();

    // Current effect
    cJSON_AddNumberToObject(root, "current_effect", programId);

    // Global parameters
    cJSON *global = cJSON_CreateObject();
    cJSON_AddNumberToObject(global, "blend_mode", global_blend_mode);
    cJSON_AddNumberToObject(global, "blend_apply_to_rec", global_blend_apply_to_rec);
    cJSON_AddNumberToObject(global, "galaxylite_damping", global_galaxylite_damping);
    cJSON_AddNumberToObject(global, "galaxylite_predelay", global_galaxylite_predelay);
    cJSON_AddNumberToObject(global, "galaxylite_mix", global_galaxylite_mix);
    cJSON_AddNumberToObject(global, "comp_threshold", global_comp_threshold);
    cJSON_AddNumberToObject(global, "comp_ratio", global_comp_ratio);
    cJSON_AddNumberToObject(global, "comp_attack", global_comp_attack);
    cJSON_AddNumberToObject(global, "comp_release", global_comp_release);
    cJSON_AddNumberToObject(global, "comp_makeup", global_comp_makeup);
    cJSON_AddNumberToObject(global, "gate_threshold", global_gate_threshold);
    cJSON_AddNumberToObject(global, "gate_attack", global_gate_attack);
    cJSON_AddNumberToObject(global, "gate_hold", global_gate_hold);
    cJSON_AddNumberToObject(global, "gate_release", global_gate_release);
    // EQ params
    cJSON_AddNumberToObject(global, "eq_enable", global_eq_enable);
    cJSON_AddNumberToObject(global, "eq_low_gain", global_eq_low_gain);
    cJSON_AddNumberToObject(global, "eq_mid_gain", global_eq_mid_gain);
    cJSON_AddNumberToObject(global, "eq_high_gain", global_eq_high_gain);
    cJSON_AddNumberToObject(global, "eq_low_freq", global_eq_low_freq);
    cJSON_AddNumberToObject(global, "eq_mid_freq", global_eq_mid_freq);
    cJSON_AddNumberToObject(global, "eq_high_freq", global_eq_high_freq);
    cJSON_AddItemToObject(root, "global", global);

    // Effect-specific uart_only parameters
    cJSON *effects = cJSON_CreateArray();
    const char *effect_names[] = {"galaxy", "reverb", "ampsim", "nam", "distortion", "delay", "tremolo", "chorus"};

    for (int e = 0; e < 8; e++) {
        cJSON *effect = cJSON_CreateObject();
        cJSON_AddNumberToObject(effect, "id", e);
        cJSON_AddStringToObject(effect, "name", effect_names[e]);

        // Only include params that have been set (!= 255)
        cJSON *params = cJSON_CreateObject();

        uint8_t count = uart_only_params[e][0];
        for (uint8_t i = 1; i <= count; i++) {
            uint8_t param_id = uart_only_params[e][i];
            uint8_t param_idx = param_id - EFFECT_PARAM_BASE;
            if (param_idx < EFFECT_PARAM_COUNT) {
                uint8_t value = effect_params[e][param_idx];
                char key[8];
                snprintf(key, sizeof(key), "%d", param_id);
                if (value != 255) {
                    cJSON_AddNumberToObject(params, key, value);
                } else {
                    cJSON_AddNullToObject(params, key);  // Not set, show as null
                }
            }
        }

        if (count > 0) {
            cJSON_AddItemToObject(effect, "params", params);
            cJSON_AddItemToArray(effects, effect);
        } else {
            cJSON_Delete(params);
            cJSON_Delete(effect);
        }
    }
    cJSON_AddItemToObject(root, "effects", effects);

    // Knob values (from Daisy)
    cJSON *knobs = cJSON_CreateObject();
    if (knob_values_received) {
        cJSON *values = cJSON_CreateArray();
        for (int i = 0; i < 6; i++) {
            cJSON_AddItemToArray(values, cJSON_CreateNumber(seed_knob_values[i]));
        }
        cJSON_AddItemToObject(knobs, "values", values);
        cJSON_AddNumberToObject(knobs, "effect", seed_knob_effect);
        cJSON_AddNumberToObject(knobs, "toggle", seed_knob_toggle);
    } else {
        cJSON_AddNullToObject(knobs, "values");
        cJSON_AddNullToObject(knobs, "effect");
        cJSON_AddNullToObject(knobs, "toggle");
    }
    cJSON_AddItemToObject(root, "knobs", knobs);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
    static struct file_server_data *server_data = NULL;

    if (server_data)
    {
        ESP_LOGE(TAG, "File server already started");
        return NULL;
    }

    /* Allocate memory for server data */
    server_data = (file_server_data *)heap_caps_malloc(sizeof(struct file_server_data), MALLOC_CAP_8BIT);
    if (!server_data)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return NULL;
    }

    // strlcpy(server_data->base_path, base_path,
    // sizeof(server_data->base_path));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 4096 * 4;  // Increase stack size for better performance
    config.max_uri_handlers = 20;  // Increased for new endpoints
    config.send_wait_timeout = 10;  // 10 second send timeout
    config.recv_wait_timeout = 10;  // 10 second receive timeout

    const httpd_uri_t wav_handler = {
        .uri = "/api/wav/*",
        .method = HTTP_GET,
        .handler = handle_wav_file,
        .user_ctx = server_data};

    const httpd_uri_t wav_resumable_handler = {
        .uri = "/api/wav/resumable/*",
        .method = HTTP_GET,
        .handler = handle_wav_file_resumable,
        .user_ctx = server_data};

    const httpd_uri_t list_handler = {
        .uri = "/api/list",
        .method = HTTP_GET,
        .handler = handle_list_files,
        .user_ctx = server_data};

    const httpd_uri_t delete_handler = {
        .uri = "/api/delete/*",
        .method = HTTP_DELETE,
        .handler = handle_delete_file,
    };

    const httpd_uri_t format_handler = {
        .uri = "/api/format",
        .method = HTTP_POST,
        .handler = handle_format_sdcard,
        .user_ctx = NULL
    };

    const httpd_uri_t config_uri = {
        .uri = "/api/wificonfig",
        .method = HTTP_POST,
        .handler = config_handler,
        .user_ctx = NULL
    };

    const httpd_uri_t record_handler = {
        .uri = "/api/recording",
        .method = HTTP_POST,
        .handler = record_event_handler,
        .user_ctx = NULL
    };

    const httpd_uri_t program_handler = {
        .uri = "/api/program",
        .method = HTTP_POST,
        .handler = program_change_event_handler,
        .user_ctx = NULL
    };


    const httpd_uri_t effect_handler = {
        .uri = "/api/effect",
        .method = HTTP_POST,
        .handler = effect_change_event_handler,
        .user_ctx = NULL
    };


    const httpd_uri_t control_handler = {
        .uri = "/api/control",
        .method = HTTP_POST,
        .handler = control_change_event_handler,
        .user_ctx = NULL
    };


    httpd_uri_t wifi_scan_uri = {
        .uri       = "/api/wifiscan",
        .method    = HTTP_GET,
        .handler   = wifi_scan_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t status_uri = {
        .uri       = "/api/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    const httpd_uri_t dfu_handler = {
        .uri = "/api/dfu",
        .method = HTTP_POST,
        .handler = handle_seed_dfu,
        .user_ctx = NULL
    };

    const httpd_uri_t setparam_handler = {
        .uri = "/api/setparam",
        .method = HTTP_POST,
        .handler = set_parameter_handler,
        .user_ctx = NULL
    };

    const httpd_uri_t selecteffect_handler = {
        .uri = "/api/selecteffect",
        .method = HTTP_POST,
        .handler = select_effect_handler,
        .user_ctx = NULL
    };

    const httpd_uri_t effectsconfig_handler = {
        .uri = "/api/effects_config",
        .method = HTTP_GET,
        .handler = effects_config_handler,
        .user_ctx = NULL
    };

    const httpd_uri_t getparams_handler = {
        .uri = "/api/params",
        .method = HTTP_GET,
        .handler = get_params_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &delete_handler);
        httpd_register_uri_handler(server, &format_handler);
        httpd_register_uri_handler(server, &list_handler);
        httpd_register_uri_handler(server, &wav_resumable_handler);
        httpd_register_uri_handler(server, &wav_handler);
        httpd_register_uri_handler(server, &config_uri);
        httpd_register_uri_handler(server, &record_handler);
        httpd_register_uri_handler(server, &wifi_scan_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &program_handler);
        httpd_register_uri_handler(server, &effect_handler);
        httpd_register_uri_handler(server, &control_handler);
        httpd_register_uri_handler(server, &dfu_handler);
        httpd_register_uri_handler(server, &setparam_handler);
        httpd_register_uri_handler(server, &selecteffect_handler);
        httpd_register_uri_handler(server, &effectsconfig_handler);
        httpd_register_uri_handler(server, &getparams_handler);

        ESP_LOGI(TAG, "**** HTTP Server started ****");
        return server;
    }
    ESP_LOGE(TAG, "**** HTTP Server start FAILED ****");
    return NULL;
}

void server_task(void *pvParameters) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi_event_group = xEventGroupCreate();

    ap_netif = esp_netif_create_default_wifi_ap();
    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Start DHCP client    
    // ESP_ERROR_CHECK(esp_netif_dhcpc_start(sta_netif));
    
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(ap_ssid, sizeof(ap_ssid), "Hoopi-%02X%02X", mac[4], mac[5]);
    try_connect_saved_wifi();
    start_webserver();

    while (1)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

#define BROADCAST_PORT 16485
#define RESPONSE_PORT 16488
#define BROADCAST_INTERVAL_MS 5000


// Static buffer for device info JSON to avoid repeated allocations
static char device_info_buffer[600];
static bool device_info_initialized = false;

// Track previous values of changing fields
static bool last_is_recording = false;
static int last_error_count = -1;
static char last_filename[64] = {0};
static size_t last_written_bytes = 0;

static void update_device_info_json(void)
{
    // Check if any changing fields have actually changed
    char truncated_filename[64];
    strncpy(truncated_filename, filename, sizeof(truncated_filename) - 1);
    truncated_filename[sizeof(truncated_filename) - 1] = '\0';

    bool needs_update = !device_info_initialized ||
                       (last_is_recording != is_recording) ||
                       (last_error_count != error_count) ||
                       (last_written_bytes != written_bytes) ||
                       (strcmp(last_filename, truncated_filename) != 0);

    if (!needs_update) {
        return;  // No changes, keep existing JSON
    }

    // Update cached values
    last_is_recording = is_recording;
    last_error_count = error_count;
    last_written_bytes = written_bytes;
    strncpy(last_filename, truncated_filename, sizeof(last_filename));

    // Build version string from firmware versions
    char version_str[16];
    snprintf(version_str, sizeof(version_str), "%d/%d", esp32_fw_version, seed_fw_version);

    // Calculate duration (shows current recording duration or last recording duration)
    size_t duration_seconds = written_bytes / BYTE_RATE;

    // Format JSON manually into static buffer
    snprintf(device_info_buffer, sizeof(device_info_buffer),
        "{\"name\":\"%s\",\"type\":\"Hoopi\",\"version\":\"%s\",\"is_recording\":%s,\"error_count\":%d,\"filename\":\"%s\",\"duration\":%zu}",
        ap_ssid,
        version_str,
        is_recording ? "true" : "false",
        error_count,
        truncated_filename,
        duration_seconds
    );

    device_info_initialized = true;
}

void udp_discovery_task(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    // Enable broadcast
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "Failed to set socket broadcast option: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(RESPONSE_PORT);
    dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    vTaskDelay(pdMS_TO_TICKS(BROADCAST_INTERVAL_MS)); //wait for IP address

    // Initialize device info JSON
    update_device_info_json();

    while (1) {
        // Update device info before each broadcast
        update_device_info_json();

        int err = sendto(sock, device_info_buffer, strlen(device_info_buffer), 0,
                        (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Error sending discovery packet: errno %d", errno);
        }

        vTaskDelay(pdMS_TO_TICKS(BROADCAST_INTERVAL_MS));
    }

    // Cleanup (unreachable in normal operation)
    close(sock);
    vTaskDelete(NULL);
}

#endif /* F444CB5E_C703_4781_9351_D08DF8012A5E */
