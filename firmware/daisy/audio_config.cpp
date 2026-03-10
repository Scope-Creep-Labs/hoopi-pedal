/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "audio_config.h"

// Output blend mode settings
uint8_t outputBlendMode = 1;        // 0=stereo, 1=mono center, 2=mono L, 3=mono R, 4-255=blend ratio
uint8_t applyBlendToRecording = 0;  // 0=recording stays stereo, 1=apply blend to recording outputs too

// GalaxyLite reverb parameters
uint8_t galaxyLiteDamping = 140;    // 0-255 maps to 0.0-1.0 (default 140 ≈ 0.55)
uint8_t galaxyLitePreDelay = 128;   // 0-255 maps to 0.0-1.0 (default 128 ≈ 0.5)
uint8_t galaxyLiteMix = 77;         // 0-255 maps to 0.0-1.0 (default 77 ≈ 0.3)

// Compressor parameters (param_id 4-8)
uint8_t compThreshold = 102;        // 0-255 maps to -60dB to 0dB (default 102 ≈ -20dB)
uint8_t compRatio = 40;             // 0-255 maps to 1:1 to 20:1 (default 40 ≈ 4:1)
uint8_t compAttack = 5;             // 0-255 maps to 1ms to 500ms (default 5 ≈ 10ms)
uint8_t compRelease = 12;           // 0-255 maps to 10ms to 2000ms (default 12 ≈ 100ms)
uint8_t compMakeupGain = 85;        // 0-255 maps to 1.0x to 4.0x (default 85 ≈ 1.5x)

// Noise Gate parameters (param_id 9-12)
uint8_t gateThreshold = 128;        // 0-255 maps to -80dB to -20dB (default 128 ≈ -50dB)
uint8_t gateAttack = 5;             // 0-255 maps to 0.1ms to 50ms (default 5 ≈ 1ms)
uint8_t gateHold = 26;              // 0-255 maps to 0ms to 500ms (default 26 ≈ 50ms)
uint8_t gateRelease = 12;           // 0-255 maps to 10ms to 2000ms (default 12 ≈ 100ms)

// 3-Band EQ parameters (param_id 30-36)
uint8_t eqEnabled = 0;              // 0 = disabled, 1 = enabled
uint8_t eqLowGain = 128;            // 0-255 maps to -12dB to +12dB (128 = unity/0dB)
uint8_t eqMidGain = 128;            // 0-255 maps to -12dB to +12dB
uint8_t eqHighGain = 128;           // 0-255 maps to -12dB to +12dB
uint8_t eqLowFreq = 85;             // 0-255 maps to 50Hz to 500Hz (default 85 ≈ 200Hz)
uint8_t eqMidFreq = 51;             // 0-255 maps to 250Hz to 4kHz (default 51 ≈ 1kHz)
uint8_t eqHighFreq = 64;            // 0-255 maps to 2kHz to 10kHz (default 64 ≈ 4kHz)

// Input processing master enable
bool inputProcessingEnabled = true;  // Enabled by default

// Backing track mixing parameters
bool backingTrackRecordBlend = false;  // If true, blend backing track into recording outputs
uint8_t backingTrackBlendRatio = 0;    // 0-127: 0=live only, 127=equal mix (0.5)
bool backingTrackBlendMic = false;     // If true, also blend backing mic (right) into live mic
