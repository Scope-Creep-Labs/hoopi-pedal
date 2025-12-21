/**
 * Hoopi Pedal - Effects module
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef GALAXY_LITE_H
#define GALAXY_LITE_H

#include <array>
#include <cmath>
#include <cstring>
#include "daisy_seed.h"

/**
 * GalaxyLite - Optimized mono reverb for combined effects
 * - 4 channels FDN with prime-number delay ratios
 * - 2 diffusion steps
 * - Parameter smoothing to prevent clicks
 * - Denormal prevention
 */

namespace bkshepherd {

class GalaxyLite {
public:
    static constexpr int CHANNELS = 4;
    static constexpr int DIFFUSION_STEPS = 2;
    static constexpr int MAX_PREDELAY = 2400;      // 50ms @ 48kHz
    static constexpr int MAX_FDN_DELAY = 9600;     // 200ms
    static constexpr int MAX_DIFFUSER_DELAY = 1200; // 25ms

    // Prime number multipliers for delay times (avoids metallic resonance)
    static constexpr float FDN_DELAY_MULT[CHANNELS] = {1.0f, 1.127f, 1.319f, 1.513f};
    static constexpr float DIFF_DELAY_MULT[DIFFUSION_STEPS][CHANNELS] = {
        {0.11f, 0.17f, 0.23f, 0.31f},
        {0.13f, 0.19f, 0.29f, 0.37f}
    };

    GalaxyLite() : m_sampleRate(48000.0f), m_roomSizeMs(100.0f),
                   m_decayGain(0.85f), m_decayGainTarget(0.85f),
                   m_wetGain(0.5f), m_dryGain(0.5f),
                   m_dampingCoeff(0.5f), m_dampingCoeffTarget(0.5f),
                   m_preDelaySamples(0), m_preDelayWriteIdx(0),
                   m_dcBlockerState(0.0f) {

        for (int c = 0; c < CHANNELS; c++) {
            m_fdnWriteIdx[c] = 0;
            m_fdnDelaySamples[c] = 0;
            m_dampingState[c] = 0.0f;
        }
        for (int s = 0; s < DIFFUSION_STEPS; s++) {
            for (int c = 0; c < CHANNELS; c++) {
                m_diffuserWriteIdx[s][c] = 0;
                m_diffuserDelaySamples[s][c] = 0;
            }
        }
    }

    void Init(float sampleRate, float* preDelayBuf,
              float* fdnBuf0, float* fdnBuf1, float* fdnBuf2, float* fdnBuf3,
              float diffuserBuf[DIFFUSION_STEPS][CHANNELS][MAX_DIFFUSER_DELAY]) {
        m_sampleRate = sampleRate;

        // Smoothing coefficient (~5ms time constant)
        m_smoothingCoeff = 1.0f - std::exp(-1.0f / (0.005f * sampleRate));

        m_preDelayBuffer = preDelayBuf;
        m_fdnBuffers[0] = fdnBuf0;
        m_fdnBuffers[1] = fdnBuf1;
        m_fdnBuffers[2] = fdnBuf2;
        m_fdnBuffers[3] = fdnBuf3;

        for (int s = 0; s < DIFFUSION_STEPS; s++) {
            for (int c = 0; c < CHANNELS; c++) {
                m_diffuserBuffers[s][c] = diffuserBuf[s][c];
            }
        }

        // Clear buffers
        std::memset(preDelayBuf, 0, MAX_PREDELAY * sizeof(float));
        for (int c = 0; c < CHANNELS; c++) {
            std::memset(m_fdnBuffers[c], 0, MAX_FDN_DELAY * sizeof(float));
            m_fdnWriteIdx[c] = 0;
            m_dampingState[c] = 0.0f;
        }
        for (int s = 0; s < DIFFUSION_STEPS; s++) {
            for (int c = 0; c < CHANNELS; c++) {
                std::memset(m_diffuserBuffers[s][c], 0, MAX_DIFFUSER_DELAY * sizeof(float));
                m_diffuserWriteIdx[s][c] = 0;
            }
        }
        m_preDelayWriteIdx = 0;
        m_dcBlockerState = 0.0f;

        // Setup default parameters
        SetSize(0.4f);
        SetDecay(0.5f);
        SetDamping(0.5f);
        SetPreDelay(0.1f);
        SetMix(0.5f);

        // Initialize smoothed values to targets
        m_decayGain = m_decayGainTarget;
        m_dampingCoeff = m_dampingCoeffTarget;
    }

    void SetSize(float size) {
        // size: 0-1, maps to 30-180ms (narrower range, more usable)
        float roomSizeMs = 30.0f + size * 150.0f;
        float delaySamplesBase = roomSizeMs * 0.001f * m_sampleRate;

        // FDN delay times using fixed prime ratios (no randomness!)
        for (int c = 0; c < CHANNELS; c++) {
            m_fdnDelaySamples[c] = (int)(FDN_DELAY_MULT[c] * delaySamplesBase);
            if (m_fdnDelaySamples[c] >= MAX_FDN_DELAY) m_fdnDelaySamples[c] = MAX_FDN_DELAY - 1;
            if (m_fdnDelaySamples[c] < 1) m_fdnDelaySamples[c] = 1;
        }

        // Diffuser delays using fixed ratios (deterministic!)
        for (int s = 0; s < DIFFUSION_STEPS; s++) {
            for (int c = 0; c < CHANNELS; c++) {
                m_diffuserDelaySamples[s][c] = (int)(DIFF_DELAY_MULT[s][c] * delaySamplesBase);
                if (m_diffuserDelaySamples[s][c] >= MAX_DIFFUSER_DELAY)
                    m_diffuserDelaySamples[s][c] = MAX_DIFFUSER_DELAY - 1;
                if (m_diffuserDelaySamples[s][c] < 1) m_diffuserDelaySamples[s][c] = 1;
            }
        }

        m_roomSizeMs = roomSizeMs;
    }

    void SetDecay(float decay) {
        // decay: 0-1, maps to RT60 of 0.3-10s
        float rt60Sec = 0.3f + decay * 9.7f;
        float typicalLoopMs = m_roomSizeMs * 1.5f;
        float loopsPerRt60 = rt60Sec / (typicalLoopMs * 0.001f);
        float dbPerCycle = -60.0f / loopsPerRt60;
        m_decayGainTarget = std::pow(10.0f, dbPerCycle * 0.05f);
        if (m_decayGainTarget > 0.98f) m_decayGainTarget = 0.98f;
        if (m_decayGainTarget < 0.0f) m_decayGainTarget = 0.0f;
    }

    void SetDamping(float damping) {
        // damping: 0-1, higher = darker
        float dampFreqHz = 12000.0f - damping * 11000.0f;
        float omega = 2.0f * 3.14159265f * dampFreqHz / m_sampleRate;
        m_dampingCoeffTarget = std::exp(-omega);
    }

    void SetPreDelay(float preDelay) {
        // preDelay: 0-1, maps to 0-50ms
        m_preDelaySamples = (int)(preDelay * 50.0f * 0.001f * m_sampleRate);
        if (m_preDelaySamples >= MAX_PREDELAY) m_preDelaySamples = MAX_PREDELAY - 1;
    }

    void SetMix(float mix) {
        m_wetGain = mix;
        m_dryGain = 1.0f - mix;
    }

    float Process(float input) {
        // Smooth parameters to prevent clicks
        m_decayGain += m_smoothingCoeff * (m_decayGainTarget - m_decayGain);
        m_dampingCoeff += m_smoothingCoeff * (m_dampingCoeffTarget - m_dampingCoeff);

        // 1. Pre-delay
        float preDelayed;
        if (m_preDelaySamples > 0) {
            int readIdx = m_preDelayWriteIdx - m_preDelaySamples;
            if (readIdx < 0) readIdx += MAX_PREDELAY;
            preDelayed = m_preDelayBuffer[readIdx];
            m_preDelayBuffer[m_preDelayWriteIdx] = input;
            m_preDelayWriteIdx++;
            if (m_preDelayWriteIdx >= MAX_PREDELAY) m_preDelayWriteIdx = 0;
        } else {
            preDelayed = input;
        }

        // 2. Spread mono to 4 channels with varied gains
        std::array<float, CHANNELS> state;
        state[0] = preDelayed * 0.9f;
        state[1] = preDelayed * 0.85f;
        state[2] = preDelayed * 0.8f;
        state[3] = preDelayed * 0.75f;

        // 3. Diffusion (2 steps with Hadamard mixing)
        for (int s = 0; s < DIFFUSION_STEPS; s++) {
            std::array<float, CHANNELS> delayed;
            for (int c = 0; c < CHANNELS; c++) {
                int readIdx = m_diffuserWriteIdx[s][c] - m_diffuserDelaySamples[s][c];
                if (readIdx < 0) readIdx += MAX_DIFFUSER_DELAY;
                delayed[c] = m_diffuserBuffers[s][c][readIdx];
                m_diffuserBuffers[s][c][m_diffuserWriteIdx[s][c]] = state[c];
                m_diffuserWriteIdx[s][c]++;
                if (m_diffuserWriteIdx[s][c] >= MAX_DIFFUSER_DELAY) m_diffuserWriteIdx[s][c] = 0;
            }
            ApplyHadamard4(delayed);
            // Fixed polarity pattern (no randomness)
            delayed[1] = -delayed[1];
            delayed[3] = -delayed[3];
            state = delayed;
        }

        // 4. Read from FDN delays
        std::array<float, CHANNELS> fdnOut;
        for (int c = 0; c < CHANNELS; c++) {
            int readIdx = m_fdnWriteIdx[c] - m_fdnDelaySamples[c];
            if (readIdx < 0) readIdx += MAX_FDN_DELAY;
            fdnOut[c] = m_fdnBuffers[c][readIdx];
        }

        // 5. Householder mixing
        ApplyHouseholder4(fdnOut);

        // 6. Damping (one-pole lowpass) with denormal prevention
        for (int c = 0; c < CHANNELS; c++) {
            m_dampingState[c] = fdnOut[c] + m_dampingCoeff * (m_dampingState[c] - fdnOut[c]);
            // Flush denormals
            if (std::fabs(m_dampingState[c]) < 1e-15f) m_dampingState[c] = 0.0f;
            fdnOut[c] = m_dampingState[c];
        }

        // 7. Write back to FDN with feedback
        for (int c = 0; c < CHANNELS; c++) {
            float newVal = state[c] * 0.2f + fdnOut[c] * m_decayGain;
            // Soft clip to prevent runaway
            if (newVal > 0.95f) newVal = 0.95f + 0.05f * std::tanh((newVal - 0.95f) * 10.0f);
            if (newVal < -0.95f) newVal = -0.95f + 0.05f * std::tanh((newVal + 0.95f) * 10.0f);
            m_fdnBuffers[c][m_fdnWriteIdx[c]] = newVal;
            m_fdnWriteIdx[c]++;
            if (m_fdnWriteIdx[c] >= MAX_FDN_DELAY) m_fdnWriteIdx[c] = 0;
        }

        // 8. Mix to mono output
        float wet = (fdnOut[0] + fdnOut[1] + fdnOut[2] + fdnOut[3]) * 0.35f;

        // 9. Simple DC blocker
        float output = wet - m_dcBlockerState;
        m_dcBlockerState = wet - output * 0.995f;

        return input * m_dryGain + output * m_wetGain;
    }

private:
    void ApplyHadamard4(std::array<float, CHANNELS>& data) {
        float a = data[0], b = data[1], c = data[2], d = data[3];
        data[0] = a + b + c + d;
        data[1] = a - b + c - d;
        data[2] = a + b - c - d;
        data[3] = a - b - c + d;
        constexpr float scale = 0.5f;
        for (int i = 0; i < 4; i++) data[i] *= scale;
    }

    void ApplyHouseholder4(std::array<float, CHANNELS>& data) {
        constexpr float factor = -0.5f;
        float sum = (data[0] + data[1] + data[2] + data[3]) * factor;
        for (int i = 0; i < 4; i++) data[i] += sum;
    }

    float m_sampleRate;
    float m_smoothingCoeff;
    float m_roomSizeMs;
    float m_decayGain;
    float m_decayGainTarget;
    float m_wetGain;
    float m_dryGain;
    float m_dampingCoeff;
    float m_dampingCoeffTarget;

    // Pre-delay
    float* m_preDelayBuffer;
    int m_preDelaySamples;
    int m_preDelayWriteIdx;

    // FDN
    float* m_fdnBuffers[CHANNELS];
    int m_fdnDelaySamples[CHANNELS];
    int m_fdnWriteIdx[CHANNELS];
    float m_dampingState[CHANNELS];

    // Diffuser
    float* m_diffuserBuffers[DIFFUSION_STEPS][CHANNELS];
    int m_diffuserDelaySamples[DIFFUSION_STEPS][CHANNELS];
    int m_diffuserWriteIdx[DIFFUSION_STEPS][CHANNELS];

    // DC blocker
    float m_dcBlockerState;
};

} // namespace bkshepherd

#endif // GALAXY_LITE_H
