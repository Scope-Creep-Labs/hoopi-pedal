// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Scope Creep Labs LLC
//
// WAV header format based on ESP-IDF examples (Espressif Systems)

#ifndef FBAB8278_36AD_47BA_AEA3_6EA63CF99D89
#define FBAB8278_36AD_47BA_AEA3_6EA63CF99D89
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FORCE_INLINE inline __attribute__((always_inline)) /**< & */

#define FBIPMAX 0.999985f             /**< close to 1.0f-LSB at 16 bit */
#define FBIPMIN (-FBIPMAX)            /**< - (1 - LSB) */
#define U82F_SCALE 0.0078740f         /**< 1 / 127 */
#define F2U8_SCALE 127.0f             /**< 128 - 1 */
#define S82F_SCALE 0.0078125f         /**< 1 / (2**7) */
#define F2S8_SCALE 127.0f             /**< (2 ** 7) - 1 */
#define S162F_SCALE 3.0517578125e-05f /**< 1 / (2** 15) */
#define F2S16_SCALE 32767.0f          /**< (2 ** 15) - 1 */
#define F2S24_SCALE 8388608.0f        /**< 2 ** 23 */
#define S242F_SCALE 1.192092896e-07f  /**< 1 / (2 ** 23) */
#define S24SIGN 0x800000              /**< 2 ** 23 */
#define S322F_SCALE 4.6566129e-10f    /**< 1 / (2** 31) */
#define F2S32_SCALE 2147483647.f      /**< (2 ** 31) - 1 */

/**
    Converts float to Signed 16-bit
*/
FORCE_INLINE int16_t f2s16(float x)
{
    x = x <= FBIPMIN ? FBIPMIN : x;
    x = x >= FBIPMAX ? FBIPMAX : x;
    return (int32_t)(x * F2S16_SCALE);
}

/**
    Converts float to Signed 32-bit
 */
FORCE_INLINE int32_t f2s32(float x)
{
    x = x <= FBIPMIN ? FBIPMIN : x;
    x = x >= FBIPMAX ? FBIPMAX : x;
    return (int32_t)(x * F2S32_SCALE);
}

/**
    Converts float to Signed 24-bit
 */
FORCE_INLINE int32_t f2s24(float x)
{
    x = x <= FBIPMIN ? FBIPMIN : x;
    x = x >= FBIPMAX ? FBIPMAX : x;
    return (int32_t)(x * F2S24_SCALE);
}

/**
    Converts Signed 24-bit to float
 */
FORCE_INLINE float s242f(int32_t x)
{
    x = (x ^ S24SIGN) - S24SIGN; //sign extend aka ((x<<8)>>8)
    return (float)x * S242F_SCALE;
}

/**
 * @brief Header structure for WAV file with only one data chunk
 *
 * @note See this for reference: http://soundfile.sapp.org/doc/WaveFormat/
 *
 * @note Assignment to variables in this struct directly is only possible for little endian architectures
 *       (including Xtensa & RISC-V)
 */
typedef struct {
    struct {
        char chunk_id[4]; /*!< Contains the letters "RIFF" in ASCII form */
        uint32_t chunk_size; /*!< This is the size of the rest of the chunk following this number */
        char chunk_format[4]; /*!< Contains the letters "WAVE" */
    } descriptor_chunk; /*!< Canonical WAVE format starts with the RIFF header */
    struct {
        char subchunk_id[4]; /*!< Contains the letters "fmt " */
        uint32_t subchunk_size; /*!< This is the size of the rest of the Subchunk which follows this number */
        uint16_t audio_format; /*!< PCM = 1, values other than 1 indicate some form of compression */
        uint16_t num_of_channels; /*!< Mono = 1, Stereo = 2, etc. */
        uint32_t sample_rate; /*!< 8000, 44100, etc. */
        uint32_t byte_rate; /*!< ==SampleRate * NumChannels * BitsPerSample s/ 8 */
        uint16_t block_align; /*!< ==NumChannels * BitsPerSample / 8 */
        uint16_t bits_per_sample; /*!< 8 bits = 8, 16 bits = 16, etc. */
    } fmt_chunk; /*!< The "fmt " subchunk describes the sound data's format */
    struct {
        char subchunk_id[4]; /*!< Contains the letters "data" */
        uint32_t subchunk_size; /*!< ==NumSamples * NumChannels * BitsPerSample / 8 */
        int16_t data[0]; /*!< Holds raw audio data */
    } data_chunk; /*!< The "data" subchunk contains the size of the data and the actual sound */
} wav_header_t;

/**
 * @brief Default header for PCM format WAV files
 *
 */
#define WAV_HEADER_PCM_DEFAULT(wav_sample_size, wav_sample_bits, wav_sample_rate, wav_channel_num) { \
    .descriptor_chunk = { \
        .chunk_id = {'R', 'I', 'F', 'F'}, \
        .chunk_size = (wav_sample_size) + sizeof(wav_header_t) - 8, \
        .chunk_format = {'W', 'A', 'V', 'E'} \
    }, \
    .fmt_chunk = { \
        .subchunk_id = {'f', 'm', 't', ' '}, \
        .subchunk_size = 16, /* 16 for PCM */ \
        .audio_format = 1, /* 1 for PCM */ \
        .num_of_channels = (wav_channel_num), \
        .sample_rate = (wav_sample_rate), \
        .byte_rate = (wav_sample_bits) * (wav_sample_rate) * (wav_channel_num) / 8, \
        .block_align = (wav_sample_bits) * (wav_channel_num) / 8, \
        .bits_per_sample = (wav_sample_bits)\
    }, \
    .data_chunk = { \
        .subchunk_id = {'d', 'a', 't', 'a'}, \
        .subchunk_size = (wav_sample_size) \
    } \
}

#ifdef __cplusplus
}
#endif

#endif /* FBAB8278_36AD_47BA_AEA3_6EA63CF99D89 */
