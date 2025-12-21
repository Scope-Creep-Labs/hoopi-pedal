/**
 * Hoopi Pedal - Effects module
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef GALAXY_REVERB_MODULE_H
#define GALAXY_REVERB_MODULE_H

#include "base_effect_module.h"
#include <stdint.h>
#include <array>

#ifdef __cplusplus

/** @file galaxy_reverb_module.h
 *  FDN-based reverb using signalsmith-dsp library
 *  Based on multi-channel feedback delay network with diffusion
 */

namespace bkshepherd {

// FDN configuration - 8 channels for good density, power of 2 for Hadamard
static constexpr int GALAXY_CHANNELS = 8;
static constexpr int GALAXY_DIFFUSION_STEPS = 4;

// Maximum delay times in samples at 48kHz
static constexpr int MAX_PREDELAY_SAMPLES = 4800;      // 100ms
static constexpr int MAX_FDN_DELAY_SAMPLES = 14400;    // 300ms per channel
static constexpr int MAX_DIFFUSER_DELAY_SAMPLES = 2400; // 50ms per step

// Prime number multipliers for delay times (avoids metallic resonance)
static constexpr float FDN_DELAY_MULT[GALAXY_CHANNELS] = {
    1.0f, 1.127f, 1.319f, 1.513f, 1.709f, 1.907f, 2.111f, 2.293f
};
static constexpr float DIFF_DELAY_MULT[GALAXY_DIFFUSION_STEPS][GALAXY_CHANNELS] = {
    {0.07f, 0.11f, 0.13f, 0.17f, 0.19f, 0.23f, 0.29f, 0.31f},
    {0.08f, 0.12f, 0.14f, 0.18f, 0.20f, 0.24f, 0.28f, 0.32f},
    {0.09f, 0.13f, 0.15f, 0.19f, 0.21f, 0.25f, 0.27f, 0.33f},
    {0.10f, 0.14f, 0.16f, 0.20f, 0.22f, 0.26f, 0.30f, 0.34f}
};

class GalaxyReverbModule : public BaseEffectModule {
  public:
    GalaxyReverbModule();
    ~GalaxyReverbModule();

    void Init(float sample_rate) override;
    void ProcessMono(float in) override;
    void ProcessStereo(float inL, float inR) override;
    float GetBrightnessForLED(int led_id) const override;

  private:
    // Process a single sample through the reverb
    void ProcessSample(float inL, float inR, float& outL, float& outR);

    // Recalculate internal parameters when knobs change
    void UpdateParameters();

    // Apply Householder matrix mixing in-place
    void ApplyHouseholder(std::array<float, GALAXY_CHANNELS>& data);

    // Apply Hadamard matrix mixing in-place
    void ApplyHadamard(std::array<float, GALAXY_CHANNELS>& data);

    // Upmix stereo to multi-channel
    void StereoToMulti(float left, float right, std::array<float, GALAXY_CHANNELS>& output);

    // Downmix multi-channel to stereo
    void MultiToStereo(const std::array<float, GALAXY_CHANNELS>& input, float& left, float& right);

    float m_sampleRate;

    // Parameter ranges
    float m_sizeMinMs;
    float m_sizeMaxMs;
    float m_decayMinSec;
    float m_decayMaxSec;
    float m_dampMinHz;
    float m_dampMaxHz;
    float m_preDelayMaxMs;

    // Cached parameters (with smoothing)
    float m_decayGain;
    float m_decayGainTarget;
    float m_wetGain;
    float m_dryGain;
    float m_inputGainL;
    float m_inputGainR;
    int m_preDelaySamples;
    float m_smoothingCoeff;

    // Pre-delay buffer
    float* m_preDelayBuffer;
    int m_preDelayWriteIdx;

    // FDN delay buffers (8 channels)
    float* m_fdnBuffers[GALAXY_CHANNELS];
    int m_fdnWriteIdx[GALAXY_CHANNELS];
    int m_fdnDelaySamples[GALAXY_CHANNELS];

    // Diffuser delay buffers (4 steps x 8 channels)
    float* m_diffuserBuffers[GALAXY_DIFFUSION_STEPS][GALAXY_CHANNELS];
    int m_diffuserWriteIdx[GALAXY_DIFFUSION_STEPS][GALAXY_CHANNELS];
    int m_diffuserDelaySamples[GALAXY_DIFFUSION_STEPS][GALAXY_CHANNELS];
    bool m_diffuserFlipPolarity[GALAXY_DIFFUSION_STEPS][GALAXY_CHANNELS];

    // Simple one-pole lowpass filters for damping (one per FDN channel)
    float m_dampingState[GALAXY_CHANNELS];
    float m_dampingCoeff;
    float m_dampingCoeffTarget;

    // DC blocker state (stereo)
    float m_dcBlockerStateL;
    float m_dcBlockerStateR;
};

} // namespace bkshepherd

#endif // __cplusplus
#endif // GALAXY_REVERB_MODULE_H
