#ifndef PLAYBACK_H
#define PLAYBACK_H

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "format_wav.h"
#include <stdio.h>
#include <string.h>

// Playback state machine
typedef enum {
    PLAYBACK_STOPPED,
    PLAYBACK_PLAYING,
    PLAYBACK_PAUSED
} playback_state_t;

// Playback context
typedef struct {
    playback_state_t state;
    FILE *file;
    char filename[128];
    size_t audio_data_size;      // Size of audio data in WAV file
    size_t audio_data_offset;    // Offset where audio data starts (after WAV header)
    size_t current_position;     // Current byte position in audio data
    bool loop_enabled;
    uint32_t sample_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    bool record_blend;           // If true, blend playback into recording audio
    uint8_t blend_ratio;         // 0-127 (0.0-0.5) - always applies to output, recording if record_blend
    bool blend_mic;              // If true, also blend mic channel (right)
} playback_ctx_t;

static playback_ctx_t playback_ctx = {
    .state = PLAYBACK_STOPPED,
    .file = NULL,
    .filename = {0},
    .audio_data_size = 0,
    .audio_data_offset = 0,
    .current_position = 0,
    .loop_enabled = false,
    .sample_rate = 48000,
    .num_channels = 2,
    .bits_per_sample = 16,
    .record_blend = false,
    .blend_ratio = 64,  // Default 0.25 blend (middle of 0-0.5 range)
    .blend_mic = false
};

// TX buffers (PSRAM, separate from RX buffers)
// Buffer size matches RX: 300ms of 48kHz stereo 32-bit audio
#define TX_BUFFER_SIZE  (1 * 300 * 48 * 2 * 4)  // 115,200 bytes (300ms of 48kHz stereo 32-bit)

static uint8_t* tx_buf1 = NULL;
static uint8_t* tx_buf2 = NULL;

// Buffer state flags - opposite logic from RX (empty = ready to fill)
static volatile bool tx_buf1_ready = false;  // Data ready to send
static volatile bool tx_buf2_ready = false;

// File read buffer (16-bit samples from WAV, before expansion to 32-bit)
// Half the size since 16-bit -> 32-bit expansion doubles the data
#define FILE_READ_BUFFER_SIZE (TX_BUFFER_SIZE / 2)
static int16_t* file_read_buf = NULL;

// Task handles
static TaskHandle_t playback_file_task_handle = NULL;
static TaskHandle_t playback_i2s_task_handle = NULL;

// Flag to signal tasks to stop
static volatile bool playback_tasks_running = false;

// External I2S TX channel handle (from daisy.h)
extern i2s_chan_handle_t tx_chan;

// Initialize playback subsystem (allocate buffers, create tasks)
static esp_err_t playback_init(void)
{
    if (tx_buf1 != NULL) {
        // Already initialized
        return ESP_OK;
    }

    // Allocate TX buffers from PSRAM
    tx_buf1 = (uint8_t *)heap_caps_malloc(TX_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    tx_buf2 = (uint8_t *)heap_caps_malloc(TX_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    file_read_buf = (int16_t *)heap_caps_malloc(FILE_READ_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

    if (tx_buf1 == NULL || tx_buf2 == NULL || file_read_buf == NULL) {
        ESP_LOGE("PLAYBACK", "Failed to allocate TX buffers from PSRAM");
        return ESP_ERR_NO_MEM;
    }

    // Zero out buffers
    memset(tx_buf1, 0, TX_BUFFER_SIZE);
    memset(tx_buf2, 0, TX_BUFFER_SIZE);

    ESP_LOGI("PLAYBACK", "Playback buffers allocated: 2 x %d bytes", TX_BUFFER_SIZE);
    return ESP_OK;
}

// Convert samples of various bit depths to 32-bit for I2S TX
// I2S expects 32-bit samples with audio data left-aligned
static void convert_samples_to_32bit(uint8_t *src, int32_t *dst, size_t sample_count, uint16_t bits_per_sample)
{
    switch (bits_per_sample) {
        case 8:
            // 8-bit audio is unsigned, center at 128
            for (size_t i = 0; i < sample_count; i++) {
                dst[i] = ((int32_t)(src[i] - 128)) << 24;
            }
            break;
        case 16:
            {
                int16_t *src16 = (int16_t *)src;
                for (size_t i = 0; i < sample_count; i++) {
                    dst[i] = ((int32_t)src16[i]) << 16;
                }
            }
            break;
        case 24:
            for (size_t i = 0; i < sample_count; i++) {
                // Read 3 bytes as little-endian 24-bit sample
                int32_t s = src[i*3] | (src[i*3+1] << 8) | (src[i*3+2] << 16);
                // Sign extend from 24-bit to 32-bit
                if (s & 0x800000) s |= 0xFF000000;
                dst[i] = s << 8;
            }
            break;
        case 32:
            memcpy(dst, src, sample_count * 4);
            break;
    }
}


// File reader task - reads from SD card and fills TX buffers
static void playback_file_read_task(void *args)
{
    ESP_LOGI("PLAYBACK", "File reader task started");

    while (playback_tasks_running) {
        // Only read when playing
        if (playback_ctx.state != PLAYBACK_PLAYING) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Check if we need to fill a buffer
        uint8_t *target_buf = NULL;
        volatile bool *ready_flag = NULL;

        if (!tx_buf1_ready) {
            target_buf = tx_buf1;
            ready_flag = &tx_buf1_ready;
        } else if (!tx_buf2_ready) {
            target_buf = tx_buf2;
            ready_flag = &tx_buf2_ready;
        } else {
            // Both buffers full, wait
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Calculate bytes per sample based on file format
        uint16_t bytes_per_sample = playback_ctx.bits_per_sample / 8;
        uint16_t num_channels = playback_ctx.num_channels;

        // TX_BUFFER_SIZE is for stereo 32-bit output
        // Calculate how many output stereo frames we can fill
        size_t output_stereo_frames = TX_BUFFER_SIZE / 8;  // 8 bytes per stereo 32-bit frame

        // Calculate how many source frames to read
        // For mono source, each frame becomes one stereo output frame
        // For stereo source, each frame becomes one stereo output frame
        size_t frames_to_read = output_stereo_frames;
        size_t bytes_per_frame = bytes_per_sample * num_channels;
        size_t bytes_to_read = frames_to_read * bytes_per_frame;

        // Check if we'd read past end of audio data
        size_t remaining = playback_ctx.audio_data_size - playback_ctx.current_position;
        if (remaining < bytes_to_read) {
            bytes_to_read = remaining;
            frames_to_read = bytes_to_read / bytes_per_frame;
        }

        if (bytes_to_read == 0) {
            // End of file
            if (playback_ctx.loop_enabled) {
                // Loop back to start
                playback_ctx.current_position = 0;
                fseek(playback_ctx.file, playback_ctx.audio_data_offset, SEEK_SET);
                ESP_LOGI("PLAYBACK", "Looping playback");
                continue;
            } else {
                // Stop playback
                ESP_LOGI("PLAYBACK", "Playback reached end of file");
                playback_ctx.state = PLAYBACK_STOPPED;
                continue;
            }
        }

        // Read from file into the read buffer
        size_t bytes_read = fread(file_read_buf, 1, bytes_to_read, playback_ctx.file);
        if (bytes_read == 0) {
            ESP_LOGE("PLAYBACK", "File read error");
            playback_ctx.state = PLAYBACK_STOPPED;
            continue;
        }

        playback_ctx.current_position += bytes_read;
        size_t frames_read = bytes_read / bytes_per_frame;

        // Convert to 32-bit and handle mono-to-stereo
        int32_t *out_buf = (int32_t *)target_buf;

        if (num_channels == 2) {
            // Stereo file: convert samples directly (L R L R -> L R L R)
            size_t total_samples = frames_read * 2;  // 2 samples per frame
            convert_samples_to_32bit((uint8_t *)file_read_buf, out_buf, total_samples, playback_ctx.bits_per_sample);
        } else {
            // Mono file: duplicate each sample to L and R channels
            // First convert mono samples to a temporary buffer, then expand
            // We can use the first half of target_buf as temp since we'll expand in-place (reverse order)
            size_t mono_samples = frames_read;
            convert_samples_to_32bit((uint8_t *)file_read_buf, out_buf, mono_samples, playback_ctx.bits_per_sample);

            // Expand mono to stereo in reverse order to avoid overwriting
            for (int i = mono_samples - 1; i >= 0; i--) {
                out_buf[i * 2] = out_buf[i];      // Left
                out_buf[i * 2 + 1] = out_buf[i];  // Right (duplicate)
            }
        }

        // Calculate output bytes written (always stereo 32-bit)
        size_t output_bytes = frames_read * 8;  // stereo 32-bit = 8 bytes per frame

        // If we read less than a full buffer, zero the rest
        if (output_bytes < TX_BUFFER_SIZE) {
            memset(target_buf + output_bytes, 0, TX_BUFFER_SIZE - output_bytes);
        }

        // Mark buffer as ready
        *ready_flag = true;
    }

    ESP_LOGI("PLAYBACK", "File reader task stopped");
    vTaskDelete(NULL);
}

// I2S TX writer task - sends TX buffers to I2S
static void playback_i2s_write_task(void *args)
{
    size_t w_bytes = 0;
    static uint8_t silence_buf[1024] = {0};

    ESP_LOGI("PLAYBACK", "I2S TX task started");

    // Enable TX channel
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    while (playback_tasks_running) {
        if (playback_ctx.state == PLAYBACK_STOPPED) {
            // When stopped, don't write anything - just wait
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (playback_ctx.state == PLAYBACK_PAUSED) {
            // When paused, write silence to keep I2S running smoothly
            i2s_channel_write(tx_chan, silence_buf, sizeof(silence_buf), &w_bytes, 100);
            continue;
        }

        // Playing - send data from buffers
        if (tx_buf1_ready) {
            if (i2s_channel_write(tx_chan, tx_buf1, TX_BUFFER_SIZE, &w_bytes, 100) == ESP_OK) {
                tx_buf1_ready = false;
            }
        } else if (tx_buf2_ready) {
            if (i2s_channel_write(tx_chan, tx_buf2, TX_BUFFER_SIZE, &w_bytes, 100) == ESP_OK) {
                tx_buf2_ready = false;
            }
        } else {
            // No buffer ready - underrun, write silence
            i2s_channel_write(tx_chan, silence_buf, sizeof(silence_buf), &w_bytes, 100);
        }
    }

    // Disable TX channel when stopping
    i2s_channel_disable(tx_chan);

    ESP_LOGI("PLAYBACK", "I2S TX task stopped");
    vTaskDelete(NULL);
}

// Start playback tasks (called from app_main)
static void playback_start_tasks(void)
{
    if (playback_tasks_running) {
        return;
    }

    playback_tasks_running = true;

    xTaskCreate(playback_file_read_task, "playback_file", 4096, NULL, 5, &playback_file_task_handle);
    xTaskCreate(playback_i2s_write_task, "playback_i2s", 4096, NULL, 5, &playback_i2s_task_handle);
}

// Parse WAV header and validate format
static esp_err_t parse_wav_header(FILE *f, playback_ctx_t *ctx)
{
    wav_header_t header;

    if (fread(&header, sizeof(header), 1, f) != 1) {
        ESP_LOGE("PLAYBACK", "Failed to read WAV header");
        return ESP_FAIL;
    }

    // Validate RIFF/WAVE
    if (memcmp(header.descriptor_chunk.chunk_id, "RIFF", 4) != 0 ||
        memcmp(header.descriptor_chunk.chunk_format, "WAVE", 4) != 0) {
        ESP_LOGE("PLAYBACK", "Invalid WAV file format");
        return ESP_FAIL;
    }

    // Validate fmt chunk
    if (memcmp(header.fmt_chunk.subchunk_id, "fmt ", 4) != 0) {
        ESP_LOGE("PLAYBACK", "Invalid fmt chunk");
        return ESP_FAIL;
    }

    // Only support PCM
    if (header.fmt_chunk.audio_format != 1) {
        ESP_LOGE("PLAYBACK", "Only PCM format supported (got %d)", header.fmt_chunk.audio_format);
        return ESP_FAIL;
    }

    // Store audio properties
    ctx->sample_rate = header.fmt_chunk.sample_rate;
    ctx->num_channels = header.fmt_chunk.num_of_channels;
    ctx->bits_per_sample = header.fmt_chunk.bits_per_sample;
    ctx->audio_data_size = header.data_chunk.subchunk_size;
    ctx->audio_data_offset = sizeof(wav_header_t);

    ESP_LOGI("PLAYBACK", "WAV: %uHz, %d channels, %d bits, %u bytes audio data",
             (unsigned)ctx->sample_rate, ctx->num_channels, ctx->bits_per_sample, (unsigned)ctx->audio_data_size);

    // Validate we support this format
    if (ctx->bits_per_sample != 8 && ctx->bits_per_sample != 16 &&
        ctx->bits_per_sample != 24 && ctx->bits_per_sample != 32) {
        ESP_LOGE("PLAYBACK", "Only 8/16/24/32-bit audio supported (got %d)", ctx->bits_per_sample);
        return ESP_FAIL;
    }
    if (ctx->num_channels != 1 && ctx->num_channels != 2) {
        ESP_LOGE("PLAYBACK", "Only mono or stereo audio supported (got %d channels)", ctx->num_channels);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Start playback of a file
// record_blend: if true, Daisy will blend playback into recording audio
// blend_ratio: 0.0-0.5 (always applies to output, applies to recording if record_blend=true)
// blend_mic: if true, also blend the mic channel (right)
static esp_err_t playback_start(const char *filepath, bool loop, bool record_blend, float blend_ratio, bool blend_mic)
{
    // Stop any current playback
    if (playback_ctx.state != PLAYBACK_STOPPED) {
        playback_ctx.state = PLAYBACK_STOPPED;
        vTaskDelay(pdMS_TO_TICKS(100));  // Let tasks notice the stop
    }

    // Close any open file
    if (playback_ctx.file != NULL) {
        fclose(playback_ctx.file);
        playback_ctx.file = NULL;
    }

    // Open the file
    playback_ctx.file = fopen(filepath, "rb");
    if (playback_ctx.file == NULL) {
        ESP_LOGE("PLAYBACK", "Failed to open file: %s", filepath);
        return ESP_FAIL;
    }

    // Parse WAV header
    if (parse_wav_header(playback_ctx.file, &playback_ctx) != ESP_OK) {
        fclose(playback_ctx.file);
        playback_ctx.file = NULL;
        return ESP_FAIL;
    }

    // Store filename and settings
    strncpy(playback_ctx.filename, filepath, sizeof(playback_ctx.filename) - 1);
    playback_ctx.loop_enabled = loop;
    playback_ctx.current_position = 0;

    // Store blend settings
    playback_ctx.record_blend = record_blend;
    playback_ctx.blend_mic = blend_mic;
    // Clamp blend_ratio to 0.0-0.5 and convert to 0-127
    if (blend_ratio < 0.0f) blend_ratio = 0.0f;
    if (blend_ratio > 0.5f) blend_ratio = 0.5f;
    playback_ctx.blend_ratio = (uint8_t)(blend_ratio * 254.0f);  // 0.0->0, 0.5->127

    // Reset buffer states
    tx_buf1_ready = false;
    tx_buf2_ready = false;

    // Start playback
    playback_ctx.state = PLAYBACK_PLAYING;

    ESP_LOGI("PLAYBACK", "Started playback: %s (loop=%d, record_blend=%d, blend=%d, blend_mic=%d)",
             filepath, loop, record_blend, playback_ctx.blend_ratio, blend_mic);
    return ESP_OK;
}

// Pause playback
static esp_err_t playback_pause(void)
{
    if (playback_ctx.state != PLAYBACK_PLAYING) {
        return ESP_ERR_INVALID_STATE;
    }

    playback_ctx.state = PLAYBACK_PAUSED;
    ESP_LOGI("PLAYBACK", "Playback paused");
    return ESP_OK;
}

// Resume playback
static esp_err_t playback_resume(void)
{
    if (playback_ctx.state != PLAYBACK_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }

    playback_ctx.state = PLAYBACK_PLAYING;
    ESP_LOGI("PLAYBACK", "Playback resumed");
    return ESP_OK;
}

// Stop playback
static esp_err_t playback_stop(void)
{
    playback_ctx.state = PLAYBACK_STOPPED;
    playback_ctx.current_position = 0;

    if (playback_ctx.file != NULL) {
        fclose(playback_ctx.file);
        playback_ctx.file = NULL;
    }

    // Clear buffers
    tx_buf1_ready = false;
    tx_buf2_ready = false;

    ESP_LOGI("PLAYBACK", "Playback stopped");
    return ESP_OK;
}

// Seek to position (in milliseconds)
static esp_err_t playback_seek(uint32_t position_ms)
{
    if (playback_ctx.file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Calculate byte position
    // bytes = ms * (sample_rate / 1000) * channels * (bits / 8)
    size_t bytes_per_ms = (playback_ctx.sample_rate / 1000) *
                          playback_ctx.num_channels *
                          (playback_ctx.bits_per_sample / 8);
    size_t byte_position = position_ms * bytes_per_ms;

    // Clamp to file size
    if (byte_position >= playback_ctx.audio_data_size) {
        byte_position = playback_ctx.audio_data_size - bytes_per_ms;
    }

    // Align to frame boundary (4 bytes for stereo 16-bit)
    byte_position &= ~3;

    // Seek in file
    if (fseek(playback_ctx.file, playback_ctx.audio_data_offset + byte_position, SEEK_SET) != 0) {
        ESP_LOGE("PLAYBACK", "Seek failed");
        return ESP_FAIL;
    }

    playback_ctx.current_position = byte_position;

    // Clear buffers to force refill
    tx_buf1_ready = false;
    tx_buf2_ready = false;

    ESP_LOGI("PLAYBACK", "Seeked to %u ms (byte %u)", (unsigned)position_ms, (unsigned)byte_position);
    return ESP_OK;
}

// Get current playback status
typedef struct {
    playback_state_t state;
    char filename[128];
    uint32_t position_ms;
    uint32_t duration_ms;
    bool loop;
    bool record_blend;
    float blend_ratio;  // 0.0-0.5
    bool blend_mic;
} playback_status_t;

static playback_status_t playback_get_status(void)
{
    playback_status_t status;

    status.state = playback_ctx.state;
    strncpy(status.filename, playback_ctx.filename, sizeof(status.filename));
    status.loop = playback_ctx.loop_enabled;
    status.record_blend = playback_ctx.record_blend;
    status.blend_ratio = (float)playback_ctx.blend_ratio / 254.0f;  // Convert back to 0.0-0.5
    status.blend_mic = playback_ctx.blend_mic;

    // Calculate position in ms
    size_t bytes_per_ms = (playback_ctx.sample_rate / 1000) *
                          playback_ctx.num_channels *
                          (playback_ctx.bits_per_sample / 8);

    if (bytes_per_ms > 0) {
        status.position_ms = playback_ctx.current_position / bytes_per_ms;
        status.duration_ms = playback_ctx.audio_data_size / bytes_per_ms;
    } else {
        status.position_ms = 0;
        status.duration_ms = 0;
    }

    return status;
}

// Get backing track blend value for UART command (0-127)
static uint8_t playback_get_blend_value(void)
{
    return playback_ctx.blend_ratio;
}

// Set loop mode
static void playback_set_loop(bool loop)
{
    playback_ctx.loop_enabled = loop;
}

// Set blend settings (updates context, caller should send UART command)
static void playback_set_blend(bool record_blend, float blend_ratio, bool blend_mic)
{
    playback_ctx.record_blend = record_blend;
    playback_ctx.blend_mic = blend_mic;
    // Clamp blend_ratio to 0.0-0.5 and convert to 0-127
    if (blend_ratio < 0.0f) blend_ratio = 0.0f;
    if (blend_ratio > 0.5f) blend_ratio = 0.5f;
    playback_ctx.blend_ratio = (uint8_t)(blend_ratio * 254.0f);
}

#endif /* PLAYBACK_H */
