// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Scope Creep Labs LLC

#ifndef E5722838_2248_4F44_8C7B_8A41EC5F9C14
#define E5722838_2248_4F44_8C7B_8A41EC5F9C14

#include "driver/gpio.h"
#include "esp_check.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "format_wav.h"
#include "cJSON.h"
#include "dirent.h"
#include "driver/sdmmc_host.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"

#define SD_MOUNT_POINT      "/sdcard"

const char mount_point[] = SD_MOUNT_POINT;


size_t samples_count = BUFFER_SIZE / 4;
const uint16_t MIN_DURATION_MS = 500;

const uint8_t AUDIO_BITS_PER_SAMPLE = 16;
const size_t NUM_CHANNELS = 2;
const size_t BYTE_RATE = (SAMPLE_RATE * (AUDIO_BITS_PER_SAMPLE / 8)) * NUM_CHANNELS;
const size_t DEFAULT_REC_TIME_SECONDS = 15;

#define FILE_PATH_MAX  256


volatile bool sd_ok = false;
int32_t latest_filenum = 0;

int16_t w_buf[BUFFER_SIZE / 4];
uint8_t mac[6];

#define CONFIG_EXAMPLE_SPI_CS_GPIO         GPIO_NUM_44     
#define CONFIG_EXAMPLE_SPI_MOSI_GPIO        GPIO_NUM_9  
#define CONFIG_EXAMPLE_SPI_SCLK_GPIO        GPIO_NUM_7    
#define CONFIG_EXAMPLE_SPI_MISO_GPIO          GPIO_NUM_8  

#define SDMMC_D1_GPIO         GPIO_NUM_44     
#define SDMMC_D2_GPIO         GPIO_NUM_43     
#define SDMMC_D3_GPIO         GPIO_NUM_5     

sdmmc_host_t host = SDSPI_HOST_DEFAULT();
sdmmc_card_t *card;

bool is_recording = false;

bool init_sdcard(void)
{
    esp_err_t ret;
    ESP_LOGI(TAG, "Initializing SD card");

    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_EXAMPLE_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_EXAMPLE_SPI_MISO_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = host.max_freq_khz
    };
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return false;;
    }

    return true;
}

bool mount_sdcard_sdmmc(bool format_if_mount_failed = false)
{
    esp_err_t ret;
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    ESP_LOGI(TAG, "*** Mounting SD card (SDMMC) ***");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = 5,
        .allocation_unit_size = 32 * 1024,
    };


    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = {
        .clk = CONFIG_EXAMPLE_SPI_SCLK_GPIO,
        .cmd = CONFIG_EXAMPLE_SPI_MOSI_GPIO,
        .d0 = CONFIG_EXAMPLE_SPI_MISO_GPIO,
        .d1 = GPIO_NUM_NC,
        .d2 = GPIO_NUM_NC,
        .d3 = GPIO_NUM_NC,
        .d4 = GPIO_NUM_NC,
        .d5 = GPIO_NUM_NC,
        .d6 = GPIO_NUM_NC,
        .d7 = GPIO_NUM_NC,
        .cd = GPIO_NUM_NC,
        .wp = GPIO_NUM_NC,
        .width = 1,
        .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
      };

    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return false;;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    return true;
}

bool mount_sdcard(bool format_if_mount_failed = false)
{
    esp_err_t ret;
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    ESP_LOGI(TAG, "*** Mounting SD card (SPI) ***");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = 5,
        .allocation_unit_size = 32 * 1024,
    };
    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_EXAMPLE_SPI_CS_GPIO;
    slot_config.host_id = SPI2_HOST;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return false;;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    return true;
}

FILE *wav_file = NULL;
size_t written_bytes = 0;
char filename[256+25];
char new_filename[256] = {0};
bool user_specified_filename = false;
bool current_recording_uses_filenum = false;  // Track if current recording uses auto-generated name

bool init_wav() {

    if (!sd_ok) {
        ESP_LOGW(TAG, "SD Card is not mounted. Aborting wav creation.");
        return false;
    }

    uint32_t flash_rec_time = BYTE_RATE * DEFAULT_REC_TIME_SECONDS;
    const wav_header_t wav_header =
        WAV_HEADER_PCM_DEFAULT(flash_rec_time, AUDIO_BITS_PER_SAMPLE, SAMPLE_RATE, NUM_CHANNELS);

    bool using_auto_name = !user_specified_filename;
    current_recording_uses_filenum = using_auto_name;

    if (using_auto_name) {
        sprintf(filename, SD_MOUNT_POINT "/%ld_hoopi_%02X%02X.wav", latest_filenum + 1, mac[4], mac[5]);
    }
    else {
        sprintf(filename, SD_MOUNT_POINT "/%s.wav", new_filename);
        user_specified_filename = false;
    }

    struct stat st;
    if (stat(filename, &st) == 0) {
        // Delete it if it exists
        unlink(filename);
        ESP_LOGI(TAG, "File %s exists. Deleted.", filename);
    }

    // Create new WAV file
    wav_file = fopen(filename, "w");
    if (wav_file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
        return false;
    }

    // Write the header to the WAV file
    fwrite(&wav_header, sizeof(wav_header), 1, wav_file);
    written_bytes = 0;
    ESP_LOGI(TAG, "File opened: %s ", filename);

    // Only increment filenum when using auto-generated names
    if (using_auto_name) {
        latest_filenum += 1;
        ESP_ERROR_CHECK(save_integer("latest_filenum", latest_filenum));
    }
    return true;
}

void generate_wav_json();

bool format_sdcard(void) {
    ESP_LOGI(TAG, "Formatting SD card...");

    // Abort if recording is in progress
    if (is_recording) {
        ESP_LOGE(TAG, "Cannot format SD card while recording is in progress");
        return false;
    }

    // If card is not mounted, attempt recovery mount with format_if_mount_failed
    if (card == NULL) {
        ESP_LOGW(TAG, "SD card not mounted, attempting recovery mount with auto-format...");

        if (!mount_sdcard_sdmmc(true)) {
            ESP_LOGE(TAG, "Recovery mount failed");
            return false;
        }

        ESP_LOGI(TAG, "Recovery mount successful, card formatted and mounted");
        sd_ok = true;

        // Reset file number counter
        latest_filenum = 0;
        ESP_ERROR_CHECK(save_integer("latest_filenum", latest_filenum));

        // Generate empty WAV JSON file
        generate_wav_json();

        return true;
    }

    // Clear file handle pointer without closing - finalize_wav() should have already closed it
    // Attempting to close may cause crash if file was already closed but pointer not cleared
    wav_file = NULL;

    // Allocate working buffer for formatting
    const size_t workbuf_size = 4096;
    void *workbuf = ff_memalloc(workbuf_size);
    if (workbuf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate working buffer");
        return false;
    }

    // Format the card while still mounted using drive number "0:"
    ESP_LOGI(TAG, "Formatting filesystem...");
    MKFS_PARM fmt_opts = {
        .fmt = FM_ANY,              // Auto-select FAT type
        .n_fat = 1,                 // Number of FAT copies
        .align = 0,                 // Data area alignment (0 = auto)
        .n_root = 0,                // Number of root directory entries (0 = auto)
        .au_size = 32 * 1024        // 32KB allocation unit size
    };

    // Use drive number 0 which corresponds to the mounted SD card
    FRESULT res = f_mkfs("0:", &fmt_opts, workbuf, workbuf_size);
    free(workbuf);

    if (res != FR_OK) {
        ESP_LOGE(TAG, "f_mkfs failed (%d)", res);
        return false;
    }

    ESP_LOGI(TAG, "Format complete!");

    // No need to unmount/remount - the filesystem is already formatted and mounted
    // Just ensure sd_ok flag is still set
    sd_ok = true;

    // Reset file number counter
    latest_filenum = 0;
    ESP_ERROR_CHECK(save_integer("latest_filenum", latest_filenum));
    ESP_LOGI(TAG, "File number counter reset to 0");

    // Generate empty WAV JSON file
    generate_wav_json();

    ESP_LOGI(TAG, "SD card formatted successfully");
    return true;
}

void finalize_wav() {
    if (wav_file == NULL) {
        return;
    }

    fseek(wav_file, 0, SEEK_SET);

    const wav_header_t wav_header =
        WAV_HEADER_PCM_DEFAULT(written_bytes, AUDIO_BITS_PER_SAMPLE, SAMPLE_RATE, NUM_CHANNELS);

    fwrite(&wav_header, sizeof(wav_header), 1, wav_file);

    fclose(wav_file);
    wav_file = NULL;  // Set to NULL after closing to prevent double-close

    size_t duration_seconds = written_bytes / BYTE_RATE;
    ESP_LOGI(TAG, "File written on SDCard. Bytes: %u, duration %u seconds, error count %d", written_bytes, duration_seconds, error_count);

    if (written_bytes < BUFFER_SIZE) {
        // empty-ish file

        unlink(filename);
        ESP_LOGI(TAG, "Too small (< %u). Deleted.", BUFFER_SIZE);

        // Only decrement filenum if this was an auto-generated name
        if (current_recording_uses_filenum) {
            latest_filenum -= 1;
            ESP_ERROR_CHECK(save_integer("latest_filenum", latest_filenum));
        }
    }
    else {
        generate_wav_json();
    }
}

void write_wav() {
    if (!is_recording || wav_file == NULL) {
        return;
    }

    fwrite((uint8_t *) w_buf, samples_count * 2, 1, wav_file);

    written_bytes += (samples_count*2);
}

void start_recording() {
    if (is_recording) {
        // Safety check: if recording flag is set but no file is open, reset the flag
        if (wav_file == NULL) {
            ESP_LOGW(TAG, "Recording flag was stuck (no file open), resetting...");
            is_recording = false;
        } else {
            ESP_LOGW(TAG, "Recording already in progress");
            return;
        }
    }

    if (!init_wav()) {
        ESP_LOGE(TAG, "Failed to initialize WAV file, recording not started");
        return;
    }

    is_recording = true;
    error_count = 0;
    ESP_LOGI(TAG, "Recording started ....");
}

void stop_recording() {
    if (!is_recording) {
        return;
    }

    is_recording = false;

    finalize_wav();

    ESP_LOGI(TAG, "Recording stopped ....");
}

static void status(void *args) {
    // uint8_t *r_buf;
    int32_t *i_buf;

    uint8_t active_buffer = 1;
    while (1)
    {
        // printf("Consumer: %d %d\n", buf1_full, buf2_full);
        if (buf1_full) {
            // r_buf = raw_buf1;
            i_buf = (int32_t *)raw_buf1;
            active_buffer = 1;
        }
        else if (buf2_full) {
            // r_buf = raw_buf2;
            i_buf = (int32_t *)raw_buf2;
            active_buffer = 2;
        }
        else {
            // ESP_LOGI(TAG, "No data available...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        for (size_t i = 0; i < samples_count; i++)
        {
            w_buf[i] = (i_buf[i] >> 16);
        }
        // printf("[0] raw: %x %x %x %x 24bit: %ld 16bit: %d \n\n", r_buf[0], r_buf[1], r_buf[2], r_buf[3], i_buf[0] >> 8, w_buf[0]);

        if (active_buffer == 1) {
            buf1_full = false;
        }
        else {
            buf2_full = false;
        }

        // continue;

        // printf("sum %ld", sum);
        // if (!is_recording)
        // {
        //     if (sum != 0) {
        //         start_recording();
        //         write_wav();
        //         continue;
        //     }
        // }
        if (is_recording)
        {
            // if (sum == 0) {
            //     stop_recording();
            //     continue;
            // }
            write_wav();
        }
    }
    vTaskDelete(NULL);
}

void generate_wav_json(){
    DIR *dir = opendir(mount_point);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory for JSON generation");
        return;
    }

    FILE* json_file = fopen(SD_MOUNT_POINT"/recordings.json", "w");
    if (json_file == NULL) {
        ESP_LOGE(TAG, "Unable to open recordings.json for writing");
        closedir(dir);
        return;
    }

    // Write opening bracket
    if (fwrite("[", 1, 1, json_file) != 1) {
        ESP_LOGE(TAG, "Failed to write JSON opening bracket");
        fclose(json_file);
        closedir(dir);
        return;
    }

    struct dirent *entry;
    char *ext;
    struct stat st;
    char filepath[FILE_PATH_MAX+25];
    char json_buffer[350]; // Increased buffer size to handle max filename (255) + JSON formatting
    bool first_entry = true;

    // Process files one at a time
    while ((entry = readdir(dir)) != NULL) {
        ext = strrchr(entry->d_name, '.');
        if (!(ext && strcasecmp(ext, ".wav") == 0)) {
            continue;
        }

        snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, entry->d_name);
        if (stat(filepath, &st) != 0) {
            continue;
        }

        int size_kb = st.st_size / 1024;
        int duration_sec = st.st_size / (44100 * 4);
        int modified_at = (int)st.st_mtime;

        // Truncate filename if too long to prevent buffer overflow
        char truncated_name[64];
        strncpy(truncated_name, entry->d_name, sizeof(truncated_name) - 1);
        truncated_name[sizeof(truncated_name) - 1] = '\0';

        snprintf(json_buffer, sizeof(json_buffer),
            "%s{\"name\":\"%s\",\"size_kb\":%d,\"duration_sec\":%d,\"modified_at\":%d}",
            first_entry ? "" : ",",
            truncated_name,
            size_kb,
            duration_sec,
            modified_at
        );

        if (fwrite(json_buffer, strlen(json_buffer), 1, json_file) != 1) {
            ESP_LOGE(TAG, "Failed to write JSON entry for file: %s", entry->d_name);
            break;
        }

        first_entry = false;

        // Small delay to avoid overwhelming SD card
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    closedir(dir);

    // Write closing bracket
    if (fwrite("]", 1, 1, json_file) != 1) {
        ESP_LOGE(TAG, "Failed to write JSON closing bracket");
    }

    fclose(json_file);
}


#endif /* E5722838_2248_4F44_8C7B_8A41EC5F9C14 */
