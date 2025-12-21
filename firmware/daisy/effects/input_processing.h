/**
 * Hoopi Pedal - Effects module
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef INPUT_PROCESSING_H
#define INPUT_PROCESSING_H

#include "daisysp-lgpl.h"
#include "daisysp.h"
#include <cmath>

using namespace daisysp;

// Forward declarations from hoopi.h
extern int toggleValues[3];
extern uint8_t compThreshold;
extern uint8_t compRatio;
extern uint8_t compAttack;
extern uint8_t compRelease;
extern uint8_t compMakeupGain;
extern uint8_t gateThreshold;
extern uint8_t gateAttack;
extern uint8_t gateHold;
extern uint8_t gateRelease;
extern uint8_t eqEnabled;
extern uint8_t eqLowGain;
extern uint8_t eqMidGain;
extern uint8_t eqHighGain;
extern uint8_t eqLowFreq;
extern uint8_t eqMidFreq;
extern uint8_t eqHighFreq;

/**
 * Input Processing Chain - EQ, Compressor and Noise Gate
 * Parameters configurable via UART cmd=8
 *   - Compressor: param_id 4-8
 *   - Noise Gate: param_id 9-12
 *   - EQ: param_id 13 (enable), 14-19 (bands)
 *
 * Controlled by Toggle Switch:
 *   Left (0): Clean - no processing
 *   Middle (1): Compressor only
 *   Right (2): Compressor + Noise Gate
 *
 * EQ is controlled independently via UART (disabled by default)
 */

/**
 * Simple 3-Band EQ using State Variable Filters
 * - Low band: shelving filter (boost/cut bass)
 * - Mid band: peaking filter (boost/cut mids)
 * - High band: shelving filter (boost/cut treble)
 */
class ThreeBandEQ {
public:
    void Init(float sample_rate) {
        m_sampleRate = sample_rate;
        m_lowFilter.Init(sample_rate);
        m_midFilter.Init(sample_rate);
        m_highFilter.Init(sample_rate);

        // Default frequencies
        SetLowFreq(200.0f);   // 200 Hz
        SetMidFreq(1000.0f);  // 1 kHz
        SetHighFreq(4000.0f); // 4 kHz

        // Default gains (unity = 1.0)
        m_lowGain = 1.0f;
        m_midGain = 1.0f;
        m_highGain = 1.0f;

        // Set Q/resonance for musical response
        m_lowFilter.SetRes(0.5f);
        m_midFilter.SetRes(0.5f);
        m_highFilter.SetRes(0.5f);
    }

    void SetLowFreq(float freq) {
        m_lowFilter.SetFreq(fclamp(freq, 20.0f, m_sampleRate / 3.0f));
    }

    void SetMidFreq(float freq) {
        m_midFilter.SetFreq(fclamp(freq, 100.0f, m_sampleRate / 3.0f));
    }

    void SetHighFreq(float freq) {
        m_highFilter.SetFreq(fclamp(freq, 500.0f, m_sampleRate / 3.0f));
    }

    void SetLowGain(float gain) { m_lowGain = gain; }
    void SetMidGain(float gain) { m_midGain = gain; }
    void SetHighGain(float gain) { m_highGain = gain; }

    // Set parameters from uint8_t (0-255) values
    void SetLowFreqFromByte(uint8_t value) {
        // 0-255 maps to 50Hz to 500Hz
        float freq = 50.0f + (value / 255.0f) * 450.0f;
        SetLowFreq(freq);
    }

    void SetMidFreqFromByte(uint8_t value) {
        // 0-255 maps to 250Hz to 4kHz (logarithmic would be better but linear is simpler)
        float freq = 250.0f + (value / 255.0f) * 3750.0f;
        SetMidFreq(freq);
    }

    void SetHighFreqFromByte(uint8_t value) {
        // 0-255 maps to 2kHz to 10kHz
        float freq = 2000.0f + (value / 255.0f) * 8000.0f;
        SetHighFreq(freq);
    }

    void SetLowGainFromByte(uint8_t value) {
        // 0-255 maps to -12dB to +12dB (128 = unity)
        float db = ((value / 255.0f) * 24.0f) - 12.0f;
        m_lowGain = std::pow(10.0f, db / 20.0f);
    }

    void SetMidGainFromByte(uint8_t value) {
        // 0-255 maps to -12dB to +12dB (128 = unity)
        float db = ((value / 255.0f) * 24.0f) - 12.0f;
        m_midGain = std::pow(10.0f, db / 20.0f);
    }

    void SetHighGainFromByte(uint8_t value) {
        // 0-255 maps to -12dB to +12dB (128 = unity)
        float db = ((value / 255.0f) * 24.0f) - 12.0f;
        m_highGain = std::pow(10.0f, db / 20.0f);
    }

    float Process(float in) {
        // Process through each filter
        m_lowFilter.Process(in);
        m_midFilter.Process(in);
        m_highFilter.Process(in);

        // Low shelf: use low pass output, blend with dry based on gain
        float lowOut = m_lowFilter.Low();
        float lowBlend = in + (lowOut - in) * (m_lowGain - 1.0f);
        if (m_lowGain < 1.0f) {
            // Cutting: reduce low frequencies
            lowBlend = in * m_lowGain + (in - lowOut) * (1.0f - m_lowGain);
        }

        // Mid peak: use band pass, add/subtract based on gain
        float midOut = m_midFilter.Band();
        float midBlend = lowBlend + midOut * (m_midGain - 1.0f);

        // High shelf: use high pass output, blend based on gain
        float highOut = m_highFilter.High();
        float highBlend = midBlend + (highOut - midBlend) * (m_highGain - 1.0f);
        if (m_highGain < 1.0f) {
            // Cutting: reduce high frequencies
            highBlend = midBlend * m_highGain + (midBlend - highOut) * (1.0f - m_highGain);
        }

        return highBlend;
    }

private:
    float m_sampleRate;
    Svf m_lowFilter;
    Svf m_midFilter;
    Svf m_highFilter;
    float m_lowGain;
    float m_midGain;
    float m_highGain;

    float fclamp(float val, float min, float max) {
        return val < min ? min : (val > max ? max : val);
    }
};

// Simple Noise Gate with envelope follower
class SimpleNoiseGate {
public:
    void Init(float sample_rate) {
        m_sampleRate = sample_rate;
        m_envelope = 0.0f;
        m_gateGain = 0.0f;
        m_holdCounter = 0;

        // Fixed parameters optimized for guitar/vocals
        SetThreshold(-50.0f);   // -50dB threshold
        SetAttack(0.001f);      // 1ms attack
        SetHold(0.050f);        // 50ms hold
        SetRelease(0.100f);     // 100ms release
    }

    void SetThreshold(float dbThreshold) {
        // Convert dB to linear
        m_threshold = std::pow(10.0f, dbThreshold / 20.0f);
    }

    void SetAttack(float seconds) {
        m_attackCoeff = std::exp(-1.0f / (seconds * m_sampleRate));
    }

    void SetHold(float seconds) {
        m_holdSamples = static_cast<int>(seconds * m_sampleRate);
    }

    void SetRelease(float seconds) {
        m_releaseCoeff = std::exp(-1.0f / (seconds * m_sampleRate));
    }

    // Methods to set parameters from uint8_t (0-255) values
    void SetThresholdFromByte(uint8_t value) {
        // 0-255 maps to -80dB to -20dB
        float db = -80.0f + (value / 255.0f) * 60.0f;
        SetThreshold(db);
    }

    void SetAttackFromByte(uint8_t value) {
        // 0-255 maps to 0.1ms to 50ms
        float ms = 0.1f + (value / 255.0f) * 49.9f;
        SetAttack(ms / 1000.0f);
    }

    void SetHoldFromByte(uint8_t value) {
        // 0-255 maps to 0ms to 500ms
        float ms = (value / 255.0f) * 500.0f;
        SetHold(ms / 1000.0f);
    }

    void SetReleaseFromByte(uint8_t value) {
        // 0-255 maps to 10ms to 2000ms
        float ms = 10.0f + (value / 255.0f) * 1990.0f;
        SetRelease(ms / 1000.0f);
    }

    float Process(float in) {
        // Envelope follower (peak detector)
        float absIn = std::fabs(in);
        if (absIn > m_envelope) {
            m_envelope = absIn;  // Instant attack for detection
        } else {
            m_envelope = m_envelope * 0.9995f;  // Slow decay for detection
        }

        // Gate logic
        if (m_envelope > m_threshold) {
            // Signal above threshold - open gate
            m_holdCounter = m_holdSamples;
            // Fast attack on gate gain
            m_gateGain = m_gateGain * m_attackCoeff + (1.0f - m_attackCoeff);
        } else if (m_holdCounter > 0) {
            // In hold period - keep gate open
            m_holdCounter--;
            m_gateGain = 1.0f;
        } else {
            // Below threshold and hold expired - close gate
            m_gateGain = m_gateGain * m_releaseCoeff;
        }

        return in * m_gateGain;
    }

private:
    float m_sampleRate;
    float m_envelope;
    float m_threshold;
    float m_gateGain;
    float m_attackCoeff;
    float m_releaseCoeff;
    int m_holdSamples;
    int m_holdCounter;
};

// Input processing chain
class InputProcessor {
public:
    void Init(float sample_rate) {
        m_compressor.Init(sample_rate);
        m_noiseGate.Init(sample_rate);
        m_eq.Init(sample_rate);

        // Initialize with values from global variables
        // (defaults: comp threshold -20dB, ratio 4:1, attack 10ms, release 100ms, makeup 1.5x)
        // (defaults: gate threshold -50dB, attack 1ms, hold 50ms, release 100ms)
        // (defaults: EQ disabled, all bands at unity gain)
        UpdateFromGlobals();

        m_compressorEnabled = false;
        m_noiseGateEnabled = false;
        m_eqEnabled = false;
    }

    void SetMode(int mode) {
        // Mode from toggle switch:
        // 0 = Clean (no processing)
        // 1 = Compressor only
        // 2 = Compressor + Noise Gate
        m_compressorEnabled = (mode >= 1);
        m_noiseGateEnabled = (mode >= 2);
    }

    void SetEqEnabled(bool enabled) {
        m_eqEnabled = enabled;
    }

    float Process(float in) {
        float out = in;

        // EQ first (shape tone before dynamics)
        if (m_eqEnabled) {
            out = m_eq.Process(out);
        }

        // Noise gate second (before gain stages)
        if (m_noiseGateEnabled) {
            out = m_noiseGate.Process(out);
        }

        // Compressor third
        if (m_compressorEnabled) {
            out = m_compressor.Process(out) * m_makeupGain;
        }

        return out;
    }

    bool IsCompressorEnabled() const { return m_compressorEnabled; }
    bool IsNoiseGateEnabled() const { return m_noiseGateEnabled; }
    bool IsEqEnabled() const { return m_eqEnabled; }

    // Compressor parameter setters (from uint8_t 0-255)
    void SetCompThreshold(uint8_t value) {
        // 0-255 maps to -60dB to 0dB
        float db = -60.0f + (value / 255.0f) * 60.0f;
        m_compressor.SetThreshold(db);
    }

    void SetCompRatio(uint8_t value) {
        // 0-255 maps to 1:1 to 20:1
        float ratio = 1.0f + (value / 255.0f) * 19.0f;
        m_compressor.SetRatio(ratio);
    }

    void SetCompAttack(uint8_t value) {
        // 0-255 maps to 1ms to 500ms
        float ms = 1.0f + (value / 255.0f) * 499.0f;
        m_compressor.SetAttack(ms / 1000.0f);
    }

    void SetCompRelease(uint8_t value) {
        // 0-255 maps to 10ms to 2000ms
        float ms = 10.0f + (value / 255.0f) * 1990.0f;
        m_compressor.SetRelease(ms / 1000.0f);
    }

    void SetCompMakeupGain(uint8_t value) {
        // 0-255 maps to 1.0x to 4.0x
        m_makeupGain = 1.0f + (value / 255.0f) * 3.0f;
    }

    // Noise gate parameter setters (delegate to SimpleNoiseGate)
    void SetGateThreshold(uint8_t value) { m_noiseGate.SetThresholdFromByte(value); }
    void SetGateAttack(uint8_t value) { m_noiseGate.SetAttackFromByte(value); }
    void SetGateHold(uint8_t value) { m_noiseGate.SetHoldFromByte(value); }
    void SetGateRelease(uint8_t value) { m_noiseGate.SetReleaseFromByte(value); }

    // EQ parameter setters (delegate to ThreeBandEQ)
    void SetEqLowGain(uint8_t value) { m_eq.SetLowGainFromByte(value); }
    void SetEqMidGain(uint8_t value) { m_eq.SetMidGainFromByte(value); }
    void SetEqHighGain(uint8_t value) { m_eq.SetHighGainFromByte(value); }
    void SetEqLowFreq(uint8_t value) { m_eq.SetLowFreqFromByte(value); }
    void SetEqMidFreq(uint8_t value) { m_eq.SetMidFreqFromByte(value); }
    void SetEqHighFreq(uint8_t value) { m_eq.SetHighFreqFromByte(value); }

    // Update all parameters from global variables
    void UpdateFromGlobals() {
        SetCompThreshold(compThreshold);
        SetCompRatio(compRatio);
        SetCompAttack(compAttack);
        SetCompRelease(compRelease);
        SetCompMakeupGain(compMakeupGain);
        SetGateThreshold(gateThreshold);
        SetGateAttack(gateAttack);
        SetGateHold(gateHold);
        SetGateRelease(gateRelease);
        SetEqEnabled(eqEnabled != 0);
        SetEqLowGain(eqLowGain);
        SetEqMidGain(eqMidGain);
        SetEqHighGain(eqHighGain);
        SetEqLowFreq(eqLowFreq);
        SetEqMidFreq(eqMidFreq);
        SetEqHighFreq(eqHighFreq);
    }

private:
    Compressor m_compressor;
    SimpleNoiseGate m_noiseGate;
    ThreeBandEQ m_eq;
    float m_makeupGain;
    bool m_compressorEnabled;
    bool m_noiseGateEnabled;
    bool m_eqEnabled;
};

// Global instance
InputProcessor inputProcessor;

void InitInputProcessor(float samplerate) {
    inputProcessor.Init(samplerate);
}

void UpdateInputProcessorMode() {
    inputProcessor.SetMode(toggleValues[1]);
}

// Process left channel only (guitar) - right channel (mic) stays clean
inline void ProcessInputChain(float& inL, float& inR) {
    inL = inputProcessor.Process(inL);
    // inR stays unchanged - mic input remains clean
}

#endif /* INPUT_PROCESSING_H */
