/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include <cstdint>
#include "effects/Nam/nam_models_builtin.h"

// QSPI Memory Layout for NAM Models
// Base address: 0x90240000 (after firmware staging area)
// This leaves 6MB free space for model storage
static constexpr uint32_t NAM_QSPI_BASE = 0x90240000;
static constexpr uint32_t NAM_QSPI_HEADER_SIZE = 64;
static constexpr uint32_t NAM_QSPI_DATA_START = NAM_QSPI_BASE + NAM_QSPI_HEADER_SIZE;

// Model pack header stored at NAM_QSPI_BASE (64 bytes)
struct NamModelPackHeader {
    uint32_t magic;             // 0x4E414D50 ("NAMP")
    uint32_t version;           // Pack format version (1)
    uint32_t model_count;       // Number of models (8)
    uint32_t weights_per_model; // 842
    uint32_t pack_crc32;        // CRC32 of all model data
    char     pack_id[32];       // Pack identifier (null-terminated string, e.g. "Classic Rock")
    uint32_t timestamp;         // Unix timestamp when pack was created
    uint32_t reserved[2];       // Future use (pad to 64 bytes)
};

static constexpr uint32_t NAM_PACK_MAGIC = 0x4E414D50;  // "NAMP"
static constexpr uint32_t NAM_PACK_VERSION = 1;
static constexpr size_t NAM_PACK_ID_MAX_LEN = 31;       // Max pack_id length (leaving room for null)

// UART Commands for NAM Model OTA (0x30-0x34)
static constexpr uint8_t CMD_NAM_PACK_START  = 0x30;
static constexpr uint8_t CMD_NAM_PACK_DATA   = 0x31;
static constexpr uint8_t CMD_NAM_PACK_VERIFY = 0x32;
static constexpr uint8_t CMD_NAM_PACK_COMMIT = 0x33;
static constexpr uint8_t CMD_NAM_PACK_STATUS = 0x34;

/**
 * Get pointer to model weights for given index.
 * If QSPI has valid model pack (magic == "NAMP"), returns QSPI pointer.
 * Otherwise falls back to built-in models.
 *
 * @param index Model index (0 to model_count-1)
 * @return Pointer to 842 floats, or nullptr if index out of range
 */
inline const float* GetModelWeights(int index) {
    // Check if QSPI has valid model pack
    const NamModelPackHeader* header = reinterpret_cast<const NamModelPackHeader*>(NAM_QSPI_BASE);

    if (header->magic == NAM_PACK_MAGIC) {
        // QSPI has valid model pack - check against QSPI model count
        if (index < 0 || index >= (int)header->model_count) {
            return nullptr;
        }
        // Memory-mapped: just return pointer offset into QSPI
        return reinterpret_cast<const float*>(NAM_QSPI_DATA_START + index * NAM_WEIGHTS_SIZE);
    }

    // No QSPI models - fall back to built-in
    if (index < 0 || index >= NAM_MODEL_COUNT) {
        return nullptr;
    }
    return builtInModelWeights[index];
}

/**
 * Check if QSPI model pack is present and valid.
 * @return true if QSPI has valid models, false if using built-in
 */
inline bool HasQspiModels() {
    const NamModelPackHeader* header = reinterpret_cast<const NamModelPackHeader*>(NAM_QSPI_BASE);
    return header->magic == NAM_PACK_MAGIC;
}

/**
 * Get the number of models in the current pack.
 * @return Model count (1-8) from QSPI pack, or NAM_MODEL_COUNT for built-in
 */
inline int GetModelCount() {
    const NamModelPackHeader* header = reinterpret_cast<const NamModelPackHeader*>(NAM_QSPI_BASE);
    if (header->magic == NAM_PACK_MAGIC && header->model_count >= 1 && header->model_count <= 8) {
        return header->model_count;
    }
    return NAM_MODEL_COUNT;  // Built-in count (8)
}

/**
 * Get the current model pack ID/name.
 * @return Pack ID string, or "Built-in" if using built-in models
 */
inline const char* GetModelPackId() {
    const NamModelPackHeader* header = reinterpret_cast<const NamModelPackHeader*>(NAM_QSPI_BASE);
    if (header->magic == NAM_PACK_MAGIC && header->pack_id[0] != '\0') {
        return header->pack_id;
    }
    return "Built-in";
}

/**
 * Get the QSPI model pack header (for reading timestamp, etc.)
 * @return Pointer to header, or nullptr if no valid QSPI pack
 */
inline const NamModelPackHeader* GetModelPackHeader() {
    const NamModelPackHeader* header = reinterpret_cast<const NamModelPackHeader*>(NAM_QSPI_BASE);
    if (header->magic == NAM_PACK_MAGIC) {
        return header;
    }
    return nullptr;
}

// NAM Model OTA state machine
void NamOtaInit();
void NamOtaProcessCommand(uint8_t cmd, const uint8_t* payload, uint16_t len);
