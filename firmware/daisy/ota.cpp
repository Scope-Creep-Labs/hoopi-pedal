/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "ota.h"
#include "hoopi_pedal.h"
#include "uart_protocol.h"
#include <cstring>

using namespace daisy;

// External hardware reference
extern HoopiPedal hw;
extern void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size);

// OTA state
OtaState ota;

bool ProcessOtaCommand(uint8_t cmd, uint8_t* payload, uint16_t payload_len) {
    switch (cmd) {
        case 0x20:  // OTA_START
        {
            if (payload_len >= 9) {
                ota.fw_size = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                              ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
                ota.fw_crc_expected = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) |
                                      ((uint32_t)payload[7] << 16) | ((uint32_t)payload[8] << 24);

                if (ota.fw_size == 0 || ota.fw_size > OTA_QSPI_MAX_SIZE) {
                    uint8_t nack[] = {0x02};  // ERR_FW_TOO_LARGE
                    UartSendFrame(0x15, nack, 1);
                } else {
                    // Stop audio to prevent glitches during flash operations
                    hw.seed.audio_handle.Stop();

                    // Switch QSPI to indirect mode for writing
                    hw.seed.qspi.DeInit();
                    QSPIHandle::Config qcfg = hw.seed.qspi_config;
                    qcfg.mode = QSPIHandle::Config::Mode::INDIRECT_POLLING;
                    hw.seed.qspi.Init(qcfg);

                    // Erase STAGING area (not active) - safe if power fails
                    uint32_t erase_end = OTA_QSPI_STAGING_ADDR + ota.fw_size;
                    hw.seed.qspi.Erase(OTA_QSPI_STAGING_ADDR, erase_end);

                    // Initialize OTA state
                    ota.active = true;
                    ota.bytes_written = 0;
                    ota.crc_calculated = 0xFFFFFFFF;
                    ota.total_blocks = (ota.fw_size + OTA_BLOCK_SIZE - 1) / OTA_BLOCK_SIZE;
                    ota.blocks_received = 0;

                    // Response: [BLOCK_SIZE:2] [TOTAL_BLOCKS:2]
                    uint8_t rsp[] = {
                        (uint8_t)(OTA_BLOCK_SIZE & 0xFF), (uint8_t)((OTA_BLOCK_SIZE >> 8) & 0xFF),
                        (uint8_t)(ota.total_blocks & 0xFF), (uint8_t)((ota.total_blocks >> 8) & 0xFF)
                    };
                    UartSendFrame(0x06, rsp, 4);  // ACK_DATA
                }
            }
            return true;
        }

        case 0x21:  // OTA_DATA
        {
            if (ota.active && payload_len >= 3) {
                uint16_t block_num = payload[1] | (payload[2] << 8);
                uint8_t* block_data = &payload[3];
                uint16_t block_len = payload_len - 3;

                if (block_num < ota.total_blocks) {
                    // Write to STAGING area
                    uint32_t addr = OTA_QSPI_STAGING_ADDR + (block_num * OTA_BLOCK_SIZE);

                    if (hw.seed.qspi.Write(addr, block_len, block_data) == QSPIHandle::Result::OK) {
                        // Update CRC for sequential blocks
                        if (block_num == ota.blocks_received) {
                            for (uint16_t i = 0; i < block_len; i++) {
                                ota.crc_calculated ^= block_data[i];
                                for (int j = 0; j < 8; j++) {
                                    ota.crc_calculated = (ota.crc_calculated >> 1) ^
                                        (0xEDB88320 & -(ota.crc_calculated & 1));
                                }
                            }
                            ota.blocks_received++;
                            ota.bytes_written += block_len;
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
            return true;
        }

        case 0x22:  // OTA_VERIFY
        {
            if (ota.active) {
                uint32_t crc = ~ota.crc_calculated;
                bool verified = (crc == ota.fw_crc_expected);

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
            return true;
        }

        case 0x23:  // OTA_FINISH
        {
            if (ota.active) {
                // Copy from staging to active area
                // This is the only risky window - keep it as short as possible

                // Erase active area
                uint32_t erase_end = OTA_QSPI_ACTIVE_ADDR + ota.fw_size;
                hw.seed.qspi.Erase(OTA_QSPI_ACTIVE_ADDR, erase_end);

                // Copy staging -> active in 32KB chunks
                constexpr uint32_t COPY_CHUNK_SIZE = 32768;
                uint8_t* sram_buf = new uint8_t[COPY_CHUNK_SIZE];

                bool copy_ok = true;
                if (sram_buf) {
                    uint32_t bytes_copied = 0;
                    while (bytes_copied < ota.fw_size && copy_ok) {
                        uint32_t chunk_size = ota.fw_size - bytes_copied;
                        if (chunk_size > COPY_CHUNK_SIZE) chunk_size = COPY_CHUNK_SIZE;

                        // Read chunk from staging via memory-mapped
                        hw.seed.qspi.DeInit();
                        hw.seed.qspi.Init(hw.seed.qspi_config);
                        memcpy(sram_buf, (void*)(OTA_QSPI_STAGING_ADDR + bytes_copied), chunk_size);

                        // Write chunk to active via indirect
                        hw.seed.qspi.DeInit();
                        QSPIHandle::Config qcfg = hw.seed.qspi_config;
                        qcfg.mode = QSPIHandle::Config::Mode::INDIRECT_POLLING;
                        hw.seed.qspi.Init(qcfg);

                        // Write full 32KB chunk at once
                        if (hw.seed.qspi.Write(OTA_QSPI_ACTIVE_ADDR + bytes_copied,
                                               chunk_size, sram_buf) != QSPIHandle::Result::OK) {
                            copy_ok = false;
                        }
                        bytes_copied += chunk_size;
                    }
                    delete[] sram_buf;
                } else {
                    copy_ok = false;
                }

                if (copy_ok) {
                    UartSendFrame(0x05, nullptr, 0);  // ACK
                    System::Delay(50);  // Let UART complete
                    System::ResetToBootloader(System::BootloaderMode::DAISY);
                } else {
                    uint8_t nack[] = {0x07};  // ERR_COPY_FAILED
                    UartSendFrame(0x15, nack, 1);
                    ota.active = false;
                    // Restore QSPI and audio
                    hw.seed.qspi.DeInit();
                    hw.seed.qspi.Init(hw.seed.qspi_config);
                    hw.seed.audio_handle.Start(AudioCallback);
                }
            }
            return true;
        }

        case 0x24:  // OTA_ABORT
        {
            if (ota.active) {
                ota.active = false;
                // Restore QSPI to memory-mapped mode
                hw.seed.qspi.DeInit();
                hw.seed.qspi.Init(hw.seed.qspi_config);
                // Restart audio
                hw.seed.audio_handle.Start(AudioCallback);
                UartSendFrame(0x05, nullptr, 0);  // ACK
            }
            return true;
        }

        default:
            return false;
    }
}
