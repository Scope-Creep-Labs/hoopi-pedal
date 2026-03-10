/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef UART_COMMANDS_H
#define UART_COMMANDS_H

#include <cstdint>

// Forward declarations for effect modules
namespace bkshepherd { class BaseEffectModule; }

// Effect enum (used for effect selection)
enum Effect {
    Galaxy,
    Reverb,
    AmpSim,
    NamSim,
    Distortion,
    Delay,
    Tremolo,
    ChorusSim,
    EffectCount  // Keep this last - used for range validation
};

// Current effect state
extern Effect enabled_effect;
extern bool bypass;

// Firmware version
#ifndef FW_VERSION_MAJOR
#define FW_VERSION_MAJOR 1
#endif
#ifndef FW_VERSION_MINOR
#define FW_VERSION_MINOR 9
#endif
extern uint8_t fw_version_major;
extern uint8_t fw_version_minor;

// Get effect module pointer by effect index
bkshepherd::BaseEffectModule* GetEffectModule(uint8_t effectIdx);

// Enable effect by ID and notify via UART
void EnableEffect(uint8_t newId);

// Send device info via UART
void SendDeviceInfo();

// Send toggle switch values via UART
void SendToggleValues();

// Send knob values via UART
void SendKnobValues();

// Process a received UART command
// Returns true if command was handled
bool ProcessUartCommand(uint8_t cmd, uint8_t* payload, uint16_t payload_len);

#endif /* UART_COMMANDS_H */
