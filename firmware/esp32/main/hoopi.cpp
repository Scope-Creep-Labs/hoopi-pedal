/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_sntp.h"

#include "daisy.h"
#include "sdcard.h"
#include "playback.h"
#include "server.h"
#include "uart.h"
#include "esp_psram.h"



extern "C" {
	void app_main(void);
}

#define LED_PIN GPIO_NUM_21

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void obtain_time(void)
{
    initialize_sntp();
    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    char strftime_buf[64];

    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current time: %s", strftime_buf);
}

#define SDCARD_MODE_SDMMC 1

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());

    // Configure LED pin
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    load_program_config();
    init_uart();
    xTaskCreate(rx_task, "uart_rx_task", 4096, NULL, 5, NULL);


    i2s_init_std_duplex();

    // Initialize playback subsystem (allocate TX buffers)
    playback_init();

    uint8_t retries = 5;
    uint8_t count = 0;

#ifdef SDCARD_MODE_SPI
    init_sdcard();
#endif
    while (count < retries)
    {
#ifdef SDCARD_MODE_SPI
        sd_ok = mount_sdcard();
#endif
#ifdef SDCARD_MODE_SDMMC
        sd_ok = mount_sdcard_sdmmc();
#endif

        if (!sd_ok)
        {
            ESP_LOGW(TAG, "SD Card mount failed. Retrying in 5s ....");
            vTaskDelay(pdMS_TO_TICKS(5000));
            count += 1;
            continue;
        }

        generate_wav_json();

        break;
    }

    esp_err_t ret = get_integer("latest_filenum", &latest_filenum, 1); // Default value of 0
    if (ret == ESP_OK)
    {
        printf("Retrieved latest filenum: %ld\n", latest_filenum);
    }

    size_t psram_size = esp_psram_get_size();
    printf("PSRAM size: %d bytes\n", psram_size);

    vTaskDelay(pdMS_TO_TICKS(2000));

    xTaskCreate(status, "riffpod_writer_task", 4096, NULL, 5, NULL);
    xTaskCreate(i2s_read_task, "riffpod_i2s_task", 4096, NULL, 5, NULL);

    // Start playback tasks (file reader + I2S TX writer)
    playback_start_tasks();

    gpio_set_level(LED_PIN, 1);

    xTaskCreatePinnedToCore(
        server_task,
        "server_task",
        4096,
        NULL,
        5,
        NULL,
        1 // Run on core 1
    );

    while (!wifi_started) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    obtain_time();

    xTaskCreate(udp_discovery_task, "udp_discovery_task", 4096, NULL, 5, NULL);

    // vTaskDelay(pdMS_TO_TICKS(5000));
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    printf("PSRAM size: %d bytes\n", psram_size);
}