/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "nam_models.h"
#include "hoopi_pedal.h"
#include "uart_protocol.h"
#include <cstring>

using namespace daisy;

// External hardware reference
extern HoopiPedal hw;
extern void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size);

// NAM OTA state
static struct {
    bool active = false;
    uint32_t pack_size = 0;
    uint32_t pack_crc_expected = 0;
    uint32_t bytes_written = 0;
    uint32_t crc_calculated = 0xFFFFFFFF;
    uint16_t total_blocks = 0;
    uint16_t blocks_received = 0;
    uint8_t model_count = 0;
    char pack_id[32];           // Pack identifier from START command
    uint32_t timestamp = 0;     // Unix timestamp from START command
} nam_ota;

// Block size for NAM model uploads (same as firmware OTA)
static constexpr uint16_t NAM_OTA_BLOCK_SIZE = 4096;

// Max pack data size: 8 models * 842 floats * 4 bytes = 26,944 bytes
static constexpr uint32_t NAM_MAX_PACK_SIZE = 8 * NAM_WEIGHTS_SIZE;

void NamOtaInit() {
    nam_ota.active = false;
}

void NamOtaProcessCommand(uint8_t cmd, const uint8_t* payload, uint16_t len) {
    switch (cmd) {
        case CMD_NAM_PACK_START:  // 0x30
        {
            // Payload: [model_count:1][total_size:4][crc32:4][timestamp:4][pack_id:32]
            // Minimum 9 bytes (without timestamp/pack_id for backwards compat)
            if (len >= 9) {
                nam_ota.model_count = payload[0];
                nam_ota.pack_size = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                                    ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
                nam_ota.pack_crc_expected = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) |
                                            ((uint32_t)payload[7] << 16) | ((uint32_t)payload[8] << 24);

                // Optional: timestamp (bytes 9-12)
                if (len >= 13) {
                    nam_ota.timestamp = (uint32_t)payload[9] | ((uint32_t)payload[10] << 8) |
                                        ((uint32_t)payload[11] << 16) | ((uint32_t)payload[12] << 24);
                } else {
                    nam_ota.timestamp = 0;
                }

                // Optional: pack_id (bytes 13-44, up to 31 chars + null)
                memset(nam_ota.pack_id, 0, sizeof(nam_ota.pack_id));
                if (len > 13) {
                    uint16_t id_len = len - 13;
                    if (id_len > NAM_PACK_ID_MAX_LEN) id_len = NAM_PACK_ID_MAX_LEN;
                    memcpy(nam_ota.pack_id, &payload[13], id_len);
                    nam_ota.pack_id[id_len] = '\0';
                }

                // Validate model count (1-8)
                if (nam_ota.model_count < 1 || nam_ota.model_count > 8) {
                    uint8_t nack[] = {0x01};  // ERR_INVALID_MODEL_COUNT
                    UartSendFrame(0x15, nack, 1);
                    return;
                }

                // Validate size matches model count
                uint32_t expected_size = nam_ota.model_count * NAM_WEIGHTS_SIZE;
                if (nam_ota.pack_size != expected_size || nam_ota.pack_size > NAM_MAX_PACK_SIZE) {
                    uint8_t nack[] = {0x02};  // ERR_INVALID_SIZE
                    UartSendFrame(0x15, nack, 1);
                    return;
                }

                // Stop audio to prevent glitches during flash operations
                hw.seed.audio_handle.Stop();

                // Switch QSPI to indirect mode for writing
                hw.seed.qspi.DeInit();
                QSPIHandle::Config qcfg = hw.seed.qspi_config;
                qcfg.mode = QSPIHandle::Config::Mode::INDIRECT_POLLING;
                hw.seed.qspi.Init(qcfg);

                // Erase NAM model region (data area only, not header yet)
                uint32_t erase_end = NAM_QSPI_DATA_START + nam_ota.pack_size;
                hw.seed.qspi.Erase(NAM_QSPI_DATA_START, erase_end);

                // Initialize state
                nam_ota.active = true;
                nam_ota.bytes_written = 0;
                nam_ota.crc_calculated = 0xFFFFFFFF;
                nam_ota.total_blocks = (nam_ota.pack_size + NAM_OTA_BLOCK_SIZE - 1) / NAM_OTA_BLOCK_SIZE;
                nam_ota.blocks_received = 0;

                // Response: [BLOCK_SIZE:2][TOTAL_BLOCKS:2]
                uint8_t rsp[] = {
                    (uint8_t)(NAM_OTA_BLOCK_SIZE & 0xFF), (uint8_t)((NAM_OTA_BLOCK_SIZE >> 8) & 0xFF),
                    (uint8_t)(nam_ota.total_blocks & 0xFF), (uint8_t)((nam_ota.total_blocks >> 8) & 0xFF)
                };
                UartSendFrame(0x06, rsp, 4);  // ACK_DATA
            }
            break;
        }

        case CMD_NAM_PACK_DATA:  // 0x31
        {
            // Payload: [block_num:2][data:N]
            if (nam_ota.active && len >= 3) {
                uint16_t block_num = payload[0] | (payload[1] << 8);
                const uint8_t* block_data = &payload[2];
                uint16_t block_len = len - 2;

                if (block_num < nam_ota.total_blocks) {
                    // Write to QSPI data area
                    uint32_t addr = NAM_QSPI_DATA_START + (block_num * NAM_OTA_BLOCK_SIZE);

                    if (hw.seed.qspi.Write(addr, block_len, const_cast<uint8_t*>(block_data)) == QSPIHandle::Result::OK) {
                        // Update CRC for sequential blocks
                        if (block_num == nam_ota.blocks_received) {
                            for (uint16_t i = 0; i < block_len; i++) {
                                nam_ota.crc_calculated ^= block_data[i];
                                for (int j = 0; j < 8; j++) {
                                    nam_ota.crc_calculated = (nam_ota.crc_calculated >> 1) ^
                                        (0xEDB88320 & -(nam_ota.crc_calculated & 1));
                                }
                            }
                            nam_ota.blocks_received++;
                            nam_ota.bytes_written += block_len;
                        }

                        // ACK with block number
                        uint8_t rsp[] = {(uint8_t)(block_num & 0xFF), (uint8_t)((block_num >> 8) & 0xFF)};
                        UartSendFrame(0x06, rsp, 2);
                    } else {
                        uint8_t nack[] = {0x06};  // ERR_WRITE_FAILED
                        UartSendFrame(0x15, nack, 1);
                    }
                }
            }
            break;
        }

        case CMD_NAM_PACK_VERIFY:  // 0x32
        {
            if (nam_ota.active) {
                uint32_t crc = ~nam_ota.crc_calculated;
                bool verified = (crc == nam_ota.pack_crc_expected);

                uint8_t rsp[] = {
                    (uint8_t)(crc & 0xFF), (uint8_t)((crc >> 8) & 0xFF),
                    (uint8_t)((crc >> 16) & 0xFF), (uint8_t)((crc >> 24) & 0xFF)
                };

                if (verified) {
                    UartSendFrame(0x06, rsp, 4);  // ACK_DATA with CRC
                } else {
                    UartSendFrame(0x15, rsp, 4);  // NACK with calculated CRC
                }
            }
            break;
        }

        case CMD_NAM_PACK_COMMIT:  // 0x33
        {
            if (nam_ota.active) {
                // Erase header area (64 bytes, but QSPI erase is sector-based)
                // We'll write the header - QSPI allows writing to erased area
                hw.seed.qspi.Erase(NAM_QSPI_BASE, NAM_QSPI_BASE + NAM_QSPI_HEADER_SIZE);

                // Build and write header
                NamModelPackHeader header;
                header.magic = NAM_PACK_MAGIC;
                header.version = NAM_PACK_VERSION;
                header.model_count = nam_ota.model_count;
                header.weights_per_model = NAM_WEIGHTS_PER_MODEL;
                header.pack_crc32 = nam_ota.pack_crc_expected;
                memset(header.pack_id, 0, sizeof(header.pack_id));
                strncpy(header.pack_id, nam_ota.pack_id, sizeof(header.pack_id) - 1);
                header.timestamp = nam_ota.timestamp;
                memset(header.reserved, 0, sizeof(header.reserved));

                if (hw.seed.qspi.Write(NAM_QSPI_BASE, sizeof(header),
                                       reinterpret_cast<uint8_t*>(&header)) == QSPIHandle::Result::OK) {
                    // Success - restore QSPI to memory-mapped mode
                    hw.seed.qspi.DeInit();
                    hw.seed.qspi.Init(hw.seed.qspi_config);

                    // Restart audio
                    hw.seed.audio_handle.Start(AudioCallback);

                    nam_ota.active = false;
                    UartSendFrame(0x05, nullptr, 0);  // ACK
                } else {
                    uint8_t nack[] = {0x07};  // ERR_HEADER_WRITE_FAILED
                    UartSendFrame(0x15, nack, 1);
                }
            }
            break;
        }

        case CMD_NAM_PACK_STATUS:  // 0x34
        {
            // Response: [has_qspi:1][model_count:1][timestamp:4][pack_id:32]
            uint8_t rsp[38];
            memset(rsp, 0, sizeof(rsp));

            const NamModelPackHeader* header = GetModelPackHeader();
            if (header) {
                rsp[0] = 1;  // has_qspi = true
                rsp[1] = header->model_count;
                rsp[2] = (header->timestamp >> 0) & 0xFF;
                rsp[3] = (header->timestamp >> 8) & 0xFF;
                rsp[4] = (header->timestamp >> 16) & 0xFF;
                rsp[5] = (header->timestamp >> 24) & 0xFF;
                strncpy(reinterpret_cast<char*>(&rsp[6]), header->pack_id, 31);
            } else {
                rsp[0] = 0;  // has_qspi = false, using built-in
                rsp[1] = NAM_MODEL_COUNT;
                // timestamp = 0, pack_id = empty (already zeroed)
            }

            UartSendFrame(0x34, rsp, sizeof(rsp));  // NAM_PACK_STATUS response
            break;
        }

        default:
            // Unknown command
            break;
    }
}
