/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef OTA_H
#define OTA_H

#include <cstdint>

// OTA Memory Layout in QSPI Flash
#define OTA_QSPI_ACTIVE_ADDR  0x90040000  // Active firmware (loaded by bootloader)
#define OTA_QSPI_STAGING_ADDR 0x90140000  // Staging area for incoming OTA
#define OTA_QSPI_MAX_SIZE     0x100000    // 1MB max per region
#define OTA_BLOCK_SIZE        4096        // Larger blocks = fewer UART packets

// Staging footer location and magic (bootloader reads this)
#define OTA_STAGING_FOOTER_ADDR (OTA_QSPI_STAGING_ADDR + OTA_QSPI_MAX_SIZE - 12)
#define OTA_STAGING_MAGIC       0x4F544152  // "OTAR" (OTA Ready)

// OTA State Machine
struct OtaState {
    bool active = false;
    uint32_t fw_size = 0;
    uint32_t fw_crc_expected = 0;
    uint32_t bytes_written = 0;
    uint32_t crc_calculated = 0xFFFFFFFF;
    uint16_t total_blocks = 0;
    uint16_t blocks_received = 0;
};

extern OtaState ota;

// Process OTA commands (0x20-0x24)
// Returns true if command was handled
bool ProcessOtaCommand(uint8_t cmd, uint8_t* payload, uint16_t payload_len);

#endif /* OTA_H */
