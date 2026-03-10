/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

#include <cstdint>

// Output blend mode settings
extern uint8_t outputBlendMode;        // 0=stereo, 1=mono center, 2=mono L, 3=mono R, 4-255=blend ratio
extern uint8_t applyBlendToRecording;  // 0=recording stays stereo, 1=apply blend to recording outputs too

// GalaxyLite reverb parameters (shared across effects using GalaxyLite)
extern uint8_t galaxyLiteDamping;      // 0-255 maps to 0.0-1.0
extern uint8_t galaxyLitePreDelay;     // 0-255 maps to 0.0-1.0
extern uint8_t galaxyLiteMix;          // 0-255 maps to 0.0-1.0

// Compressor parameters (param_id 4-8)
extern uint8_t compThreshold;          // 0-255 maps to -60dB to 0dB
extern uint8_t compRatio;              // 0-255 maps to 1:1 to 20:1
extern uint8_t compAttack;             // 0-255 maps to 1ms to 500ms
extern uint8_t compRelease;            // 0-255 maps to 10ms to 2000ms
extern uint8_t compMakeupGain;         // 0-255 maps to 1.0x to 4.0x

// Noise Gate parameters (param_id 9-12)
extern uint8_t gateThreshold;          // 0-255 maps to -80dB to -20dB
extern uint8_t gateAttack;             // 0-255 maps to 0.1ms to 50ms
extern uint8_t gateHold;               // 0-255 maps to 0ms to 500ms
extern uint8_t gateRelease;            // 0-255 maps to 10ms to 2000ms

// 3-Band EQ parameters (param_id 30-36)
extern uint8_t eqEnabled;              // 0 = disabled, 1 = enabled
extern uint8_t eqLowGain;              // 0-255 maps to -12dB to +12dB (128 = unity)
extern uint8_t eqMidGain;              // 0-255 maps to -12dB to +12dB
extern uint8_t eqHighGain;             // 0-255 maps to -12dB to +12dB
extern uint8_t eqLowFreq;              // 0-255 maps to 50Hz to 500Hz
extern uint8_t eqMidFreq;              // 0-255 maps to 250Hz to 4kHz
extern uint8_t eqHighFreq;             // 0-255 maps to 2kHz to 10kHz

// Input processing master enable (compressor, gate, EQ chain)
extern bool inputProcessingEnabled;

// Backing track mixing parameters (set via CMD 0x0C)
extern bool backingTrackRecordBlend;   // If true, blend backing track into recording outputs
extern uint8_t backingTrackBlendRatio; // 0-127: 0=live only, 127=equal mix (0.5)
extern bool backingTrackBlendMic;      // If true, also blend backing mic (right) into live mic

// Apply output blend based on global blend mode setting
// Recording outputs (out[2]/out[3]) always remain stereo
inline void ApplyOutputBlend(float inL, float inR, float& outL, float& outR) {
    if (outputBlendMode == 0) {
        // Stereo: L/R stay separate
        outL = inL;
        outR = inR;
    } else if (outputBlendMode == 1) {
        // Mono center: 50/50 blend to both outputs
        float mono = (inL + inR) * 0.5f;
        outL = mono;
        outR = mono;
    } else if (outputBlendMode == 2) {
        // Mono left: blend both to L, R silent
        outL = (inL + inR) * 0.5f;
        outR = 0.0f;
    } else if (outputBlendMode == 3) {
        // Mono right: blend both to R, L silent
        outL = 0.0f;
        outR = (inL + inR) * 0.5f;
    } else {
        // Blend ratio: 4-255 controls L/R mix ratio in mono output
        // 4=100% L, 128=50/50, 255=100% R
        float ratioR = (outputBlendMode - 4) / 251.0f;  // 0.0 to 1.0
        float ratioL = 1.0f - ratioR;
        float mono = inL * ratioL + inR * ratioR;
        outL = mono;
        outR = mono;
    }
}

// Mix backing track (left channel only) into guitar signal
// Only affects left channel (guitar), right channel (mic) passes through unchanged
inline float ApplyBackingTrackMix(float guitarL, float backingL) {
    if (backingTrackBlendRatio == 0) {
        return guitarL;
    }
    // 0-127 maps to 0.0-0.5 blend ratio (max 50% backing track at equal mix)
    float blendRatio = backingTrackBlendRatio / 254.0f;
    return guitarL * (1.0f - blendRatio) + backingL * blendRatio;
}

#endif /* AUDIO_CONFIG_H */
