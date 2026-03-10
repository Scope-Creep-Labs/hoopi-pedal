/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "galaxy_reverb_module.h"
#include "daisy_seed.h"
#include <cmath>
#include <cstring>

using namespace bkshepherd;

// Allocate buffers in SDRAM for large delay lines
static float DSY_SDRAM_BSS preDelayBuffer[MAX_PREDELAY_SAMPLES];
static float DSY_SDRAM_BSS fdnBuffer0[MAX_FDN_DELAY_SAMPLES];
static float DSY_SDRAM_BSS fdnBuffer1[MAX_FDN_DELAY_SAMPLES];
static float DSY_SDRAM_BSS fdnBuffer2[MAX_FDN_DELAY_SAMPLES];
static float DSY_SDRAM_BSS fdnBuffer3[MAX_FDN_DELAY_SAMPLES];
static float DSY_SDRAM_BSS fdnBuffer4[MAX_FDN_DELAY_SAMPLES];
static float DSY_SDRAM_BSS fdnBuffer5[MAX_FDN_DELAY_SAMPLES];
static float DSY_SDRAM_BSS fdnBuffer6[MAX_FDN_DELAY_SAMPLES];
static float DSY_SDRAM_BSS fdnBuffer7[MAX_FDN_DELAY_SAMPLES];

// Diffuser buffers (smaller, also in SDRAM)
static float DSY_SDRAM_BSS diffuserBuffer[GALAXY_DIFFUSION_STEPS][GALAXY_CHANNELS][MAX_DIFFUSER_DELAY_SAMPLES];

// Parameter metadata
// Knob layout: Size, Decay, Input Gain L, Damping, Mix, Input Gain R
static const int s_paramCount = 6;
static const ParameterMetaData s_metaData[s_paramCount] = {
    {
        name : "Size",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.4f},
        knobMapping : 0,
        midiCCMapping : 1
    },
    {
        name : "Decay",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 1,
        midiCCMapping : 21
    },
    {
        name : "In Gain L",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.0f},
        knobMapping : 2,
        midiCCMapping : 14,
        minValue : 1,
        maxValue : 20
    },
    {
        name : "Damping",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 3,
        midiCCMapping : 22
    },
    {
        name : "Mix",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 4,
        midiCCMapping : 23
    },
    {
        name : "In Gain R",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.0f},
        knobMapping : 5,
        midiCCMapping : 24,
        minValue : 1,
        maxValue : 20
    }
};

GalaxyReverbModule::GalaxyReverbModule()
    : BaseEffectModule(),
      m_sampleRate(48000.0f),
      m_sizeMinMs(30.0f),
      m_sizeMaxMs(180.0f),
      m_decayMinSec(0.3f),
      m_decayMaxSec(10.0f),
      m_dampMinHz(1000.0f),
      m_dampMaxHz(12000.0f),
      m_preDelayMaxMs(50.0f),
      m_decayGain(0.85f),
      m_decayGainTarget(0.85f),
      m_wetGain(0.5f),
      m_dryGain(0.5f),
      m_inputGainL(1.0f),
      m_inputGainR(1.0f),
      m_preDelaySamples(0),
      m_smoothingCoeff(0.0f),
      m_preDelayBuffer(nullptr),
      m_preDelayWriteIdx(0),
      m_dampingCoeff(0.5f),
      m_dampingCoeffTarget(0.5f),
      m_dcBlockerStateL(0.0f),
      m_dcBlockerStateR(0.0f)
{
    m_name = "Galaxy";

    m_paramMetaData = s_metaData;
    this->InitParams(s_paramCount);

    // Initialize pointers
    for (int c = 0; c < GALAXY_CHANNELS; c++) {
        m_fdnBuffers[c] = nullptr;
        m_fdnWriteIdx[c] = 0;
        m_fdnDelaySamples[c] = 0;
        m_dampingState[c] = 0.0f;
    }

    for (int step = 0; step < GALAXY_DIFFUSION_STEPS; step++) {
        for (int c = 0; c < GALAXY_CHANNELS; c++) {
            m_diffuserBuffers[step][c] = nullptr;
            m_diffuserWriteIdx[step][c] = 0;
            m_diffuserDelaySamples[step][c] = 0;
            m_diffuserFlipPolarity[step][c] = false;
        }
    }
}

GalaxyReverbModule::~GalaxyReverbModule() {
    // Buffers are static SDRAM, no cleanup needed
}

void GalaxyReverbModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);
    m_sampleRate = sample_rate;

    // Smoothing coefficient (~5ms time constant)
    m_smoothingCoeff = 1.0f - std::exp(-1.0f / (0.005f * sample_rate));

    // Assign static buffers
    m_preDelayBuffer = preDelayBuffer;
    m_fdnBuffers[0] = fdnBuffer0;
    m_fdnBuffers[1] = fdnBuffer1;
    m_fdnBuffers[2] = fdnBuffer2;
    m_fdnBuffers[3] = fdnBuffer3;
    m_fdnBuffers[4] = fdnBuffer4;
    m_fdnBuffers[5] = fdnBuffer5;
    m_fdnBuffers[6] = fdnBuffer6;
    m_fdnBuffers[7] = fdnBuffer7;

    for (int step = 0; step < GALAXY_DIFFUSION_STEPS; step++) {
        for (int c = 0; c < GALAXY_CHANNELS; c++) {
            m_diffuserBuffers[step][c] = diffuserBuffer[step][c];
        }
    }

    // Clear all buffers
    std::memset(preDelayBuffer, 0, sizeof(preDelayBuffer));
    std::memset(fdnBuffer0, 0, sizeof(fdnBuffer0));
    std::memset(fdnBuffer1, 0, sizeof(fdnBuffer1));
    std::memset(fdnBuffer2, 0, sizeof(fdnBuffer2));
    std::memset(fdnBuffer3, 0, sizeof(fdnBuffer3));
    std::memset(fdnBuffer4, 0, sizeof(fdnBuffer4));
    std::memset(fdnBuffer5, 0, sizeof(fdnBuffer5));
    std::memset(fdnBuffer6, 0, sizeof(fdnBuffer6));
    std::memset(fdnBuffer7, 0, sizeof(fdnBuffer7));
    std::memset(diffuserBuffer, 0, sizeof(diffuserBuffer));

    // Reset write indices and states
    m_preDelayWriteIdx = 0;
    m_dcBlockerStateL = 0.0f;
    m_dcBlockerStateR = 0.0f;
    for (int c = 0; c < GALAXY_CHANNELS; c++) {
        m_fdnWriteIdx[c] = 0;
        m_dampingState[c] = 0.0f;
    }
    for (int step = 0; step < GALAXY_DIFFUSION_STEPS; step++) {
        for (int c = 0; c < GALAXY_CHANNELS; c++) {
            m_diffuserWriteIdx[step][c] = 0;
        }
    }

    // Setup initial delay times using fixed prime-number ratios
    float roomSizeMs = m_sizeMinMs + 0.4f * (m_sizeMaxMs - m_sizeMinMs);
    float delaySamplesBase = roomSizeMs * 0.001f * m_sampleRate;

    // FDN delay times using fixed prime ratios (no randomness)
    for (int c = 0; c < GALAXY_CHANNELS; c++) {
        m_fdnDelaySamples[c] = (int)(FDN_DELAY_MULT[c] * delaySamplesBase);
        if (m_fdnDelaySamples[c] >= MAX_FDN_DELAY_SAMPLES) {
            m_fdnDelaySamples[c] = MAX_FDN_DELAY_SAMPLES - 1;
        }
        if (m_fdnDelaySamples[c] < 1) {
            m_fdnDelaySamples[c] = 1;
        }
    }

    // Diffuser delay times using fixed ratios (deterministic)
    for (int step = 0; step < GALAXY_DIFFUSION_STEPS; step++) {
        for (int c = 0; c < GALAXY_CHANNELS; c++) {
            m_diffuserDelaySamples[step][c] = (int)(DIFF_DELAY_MULT[step][c] * delaySamplesBase);
            if (m_diffuserDelaySamples[step][c] >= MAX_DIFFUSER_DELAY_SAMPLES) {
                m_diffuserDelaySamples[step][c] = MAX_DIFFUSER_DELAY_SAMPLES - 1;
            }
            if (m_diffuserDelaySamples[step][c] < 1) {
                m_diffuserDelaySamples[step][c] = 1;
            }
            // Fixed polarity pattern
            m_diffuserFlipPolarity[step][c] = ((step + c) & 1) != 0;
        }
    }

    UpdateParameters();

    // Initialize smoothed values to targets
    m_decayGain = m_decayGainTarget;
    m_dampingCoeff = m_dampingCoeffTarget;
}

void GalaxyReverbModule::UpdateParameters() {
    // Size (param 0) - affects delay times
    float sizeParam = GetParameterAsFloat(0);
    float roomSizeMs = m_sizeMinMs + sizeParam * (m_sizeMaxMs - m_sizeMinMs);
    float delaySamplesBase = roomSizeMs * 0.001f * m_sampleRate;

    // Update FDN delay times using fixed prime ratios
    for (int c = 0; c < GALAXY_CHANNELS; c++) {
        m_fdnDelaySamples[c] = (int)(FDN_DELAY_MULT[c] * delaySamplesBase);
        if (m_fdnDelaySamples[c] >= MAX_FDN_DELAY_SAMPLES) {
            m_fdnDelaySamples[c] = MAX_FDN_DELAY_SAMPLES - 1;
        }
        if (m_fdnDelaySamples[c] < 1) {
            m_fdnDelaySamples[c] = 1;
        }
    }

    // Update diffuser delay times using fixed ratios
    for (int step = 0; step < GALAXY_DIFFUSION_STEPS; step++) {
        for (int c = 0; c < GALAXY_CHANNELS; c++) {
            m_diffuserDelaySamples[step][c] = (int)(DIFF_DELAY_MULT[step][c] * delaySamplesBase);
            if (m_diffuserDelaySamples[step][c] >= MAX_DIFFUSER_DELAY_SAMPLES) {
                m_diffuserDelaySamples[step][c] = MAX_DIFFUSER_DELAY_SAMPLES - 1;
            }
            if (m_diffuserDelaySamples[step][c] < 1) {
                m_diffuserDelaySamples[step][c] = 1;
            }
        }
    }

    // Decay (param 1) - RT60 time (set target for smoothing)
    float decayParam = GetParameterAsFloat(1);
    float rt60Sec = m_decayMinSec + decayParam * (m_decayMaxSec - m_decayMinSec);

    // Calculate decay gain based on RT60 and typical loop time
    float typicalLoopMs = roomSizeMs * 1.5f;
    float loopsPerRt60 = rt60Sec / (typicalLoopMs * 0.001f);
    float dbPerCycle = -60.0f / loopsPerRt60;
    m_decayGainTarget = std::pow(10.0f, dbPerCycle * 0.05f);

    // Clamp decay gain to prevent instability (0.98 max like GalaxyLite)
    if (m_decayGainTarget > 0.98f) m_decayGainTarget = 0.98f;
    if (m_decayGainTarget < 0.0f) m_decayGainTarget = 0.0f;

    // Input Gain L (param 2) - 1 to 20x gain (matches Newton)
    float gainLParam = GetParameterAsFloat(2);
    m_inputGainL = 1.0f + gainLParam * 19.0f;  // 0-1 maps to 1-20x

    // Damping (param 3) - lowpass filter frequency (set target for smoothing)
    float dampParam = GetParameterAsFloat(3);
    // Invert so knob right = more damping (lower freq)
    float dampFreqHz = m_dampMaxHz - dampParam * (m_dampMaxHz - m_dampMinHz);

    // One-pole lowpass coefficient: coeff = exp(-2 * pi * fc / fs)
    float omega = 2.0f * 3.14159265f * dampFreqHz / m_sampleRate;
    m_dampingCoeffTarget = std::exp(-omega);

    // Mix (param 4)
    float mixParam = GetParameterAsFloat(4);
    m_wetGain = mixParam;
    m_dryGain = 1.0f - mixParam;

    // Input Gain R (param 5) - 1 to 20x gain (matches Newton)
    float gainRParam = GetParameterAsFloat(5);
    m_inputGainR = 1.0f + gainRParam * 19.0f;  // 0-1 maps to 1-20x

    // Fixed pre-delay (~15ms)
    m_preDelaySamples = (int)(15.0f * 0.001f * m_sampleRate);
}

void GalaxyReverbModule::ApplyHouseholder(std::array<float, GALAXY_CHANNELS>& data) {
    // Householder matrix: H = I - 2/N * ones(N)
    // Each output = input - (2/N) * sum(all inputs)
    constexpr float factor = -2.0f / GALAXY_CHANNELS;

    float sum = 0.0f;
    for (int i = 0; i < GALAXY_CHANNELS; i++) {
        sum += data[i];
    }
    sum *= factor;

    for (int i = 0; i < GALAXY_CHANNELS; i++) {
        data[i] += sum;
    }
}

void GalaxyReverbModule::ApplyHadamard(std::array<float, GALAXY_CHANNELS>& data) {
    // Hadamard matrix for 8 channels (recursive structure)
    // Apply unscaled first, then scale
    constexpr int size = GALAXY_CHANNELS;
    int hSize = size / 2;

    // Level 1: pairs
    while (hSize >= 1) {
        for (int startIndex = 0; startIndex < size; startIndex += hSize * 2) {
            for (int i = startIndex; i < startIndex + hSize; i++) {
                float a = data[i];
                float b = data[i + hSize];
                data[i] = a + b;
                data[i + hSize] = a - b;
            }
        }
        hSize /= 2;
    }

    // Scale by sqrt(1/N)
    constexpr float scale = 0.353553390593f; // sqrt(1/8)
    for (int i = 0; i < size; i++) {
        data[i] *= scale;
    }
}

void GalaxyReverbModule::StereoToMulti(float left, float right, std::array<float, GALAXY_CHANNELS>& output) {
    // Stereo to 8-channel upmix with minimal cross-feed
    // Even channels (0,2,4,6) are L-dominant, odd channels (1,3,5,7) are R-dominant
    output[0] = left;
    output[1] = right;
    output[2] = left * 0.9f;
    output[3] = right * 0.9f;
    output[4] = left * 0.8f;
    output[5] = right * 0.8f;
    output[6] = left * 0.7f;
    output[7] = right * 0.7f;
}

void GalaxyReverbModule::MultiToStereo(const std::array<float, GALAXY_CHANNELS>& input, float& left, float& right) {
    // Downmix 8 channels to stereo - keep L/R separated
    // Even channels to L, odd channels to R
    left = (input[0] + input[2] + input[4] + input[6]) * 0.35f;
    right = (input[1] + input[3] + input[5] + input[7]) * 0.35f;
}

void GalaxyReverbModule::ProcessSample(float inL, float inR, float& outL, float& outR) {
    // 0. Smooth parameters to prevent clicks
    m_decayGain += m_smoothingCoeff * (m_decayGainTarget - m_decayGain);
    m_dampingCoeff += m_smoothingCoeff * (m_dampingCoeffTarget - m_dampingCoeff);

    // 1. Apply input gains
    float gainedL = inL * m_inputGainL;
    float gainedR = inR * m_inputGainR;

    // 2. Pre-delay
    float preDelayed;
    if (m_preDelaySamples > 0) {
        int readIdx = m_preDelayWriteIdx - m_preDelaySamples;
        if (readIdx < 0) readIdx += MAX_PREDELAY_SAMPLES;
        preDelayed = m_preDelayBuffer[readIdx];
        m_preDelayBuffer[m_preDelayWriteIdx] = (gainedL + gainedR) * 0.5f;
        m_preDelayWriteIdx++;
        if (m_preDelayWriteIdx >= MAX_PREDELAY_SAMPLES) {
            m_preDelayWriteIdx = 0;
        }
    } else {
        preDelayed = (gainedL + gainedR) * 0.5f;
    }

    // 3. Upmix to 8 channels
    std::array<float, GALAXY_CHANNELS> state;
    StereoToMulti(gainedL, gainedR, state);

    // Scale input
    for (int c = 0; c < GALAXY_CHANNELS; c++) {
        state[c] *= 0.2f;
    }

    // 4. Diffusion stages (4 steps with Hadamard mixing)
    for (int step = 0; step < GALAXY_DIFFUSION_STEPS; step++) {
        std::array<float, GALAXY_CHANNELS> delayed;

        // Read from diffuser delays and write new values
        for (int c = 0; c < GALAXY_CHANNELS; c++) {
            int delaySamples = m_diffuserDelaySamples[step][c];
            int readIdx = m_diffuserWriteIdx[step][c] - delaySamples;
            if (readIdx < 0) readIdx += MAX_DIFFUSER_DELAY_SAMPLES;

            delayed[c] = m_diffuserBuffers[step][c][readIdx];
            m_diffuserBuffers[step][c][m_diffuserWriteIdx[step][c]] = state[c];

            m_diffuserWriteIdx[step][c]++;
            if (m_diffuserWriteIdx[step][c] >= MAX_DIFFUSER_DELAY_SAMPLES) {
                m_diffuserWriteIdx[step][c] = 0;
            }
        }

        // Apply Hadamard mixing
        ApplyHadamard(delayed);

        // Apply fixed polarity flips
        for (int c = 0; c < GALAXY_CHANNELS; c++) {
            if (m_diffuserFlipPolarity[step][c]) {
                delayed[c] = -delayed[c];
            }
        }

        state = delayed;
    }

    // 5. Read from FDN delay lines (no modulation for cleaner sound)
    std::array<float, GALAXY_CHANNELS> fdnOutput;
    for (int c = 0; c < GALAXY_CHANNELS; c++) {
        int readIdx = m_fdnWriteIdx[c] - m_fdnDelaySamples[c];
        if (readIdx < 0) readIdx += MAX_FDN_DELAY_SAMPLES;
        fdnOutput[c] = m_fdnBuffers[c][readIdx];
    }

    // 6. Apply Householder mixing to FDN output
    ApplyHouseholder(fdnOutput);

    // 7. Apply damping filters (one-pole lowpass) with denormal prevention
    for (int c = 0; c < GALAXY_CHANNELS; c++) {
        m_dampingState[c] = fdnOutput[c] + m_dampingCoeff * (m_dampingState[c] - fdnOutput[c]);
        // Flush denormals
        if (std::fabs(m_dampingState[c]) < 1e-15f) m_dampingState[c] = 0.0f;
        fdnOutput[c] = m_dampingState[c];
    }

    // 8. Write back to FDN delays: input from diffuser + feedback
    for (int c = 0; c < GALAXY_CHANNELS; c++) {
        float newValue = state[c] + fdnOutput[c] * m_decayGain;

        // Soft clip with tanh (smoother than rational function)
        if (newValue > 0.95f) newValue = 0.95f + 0.05f * std::tanh((newValue - 0.95f) * 10.0f);
        if (newValue < -0.95f) newValue = -0.95f + 0.05f * std::tanh((newValue + 0.95f) * 10.0f);

        m_fdnBuffers[c][m_fdnWriteIdx[c]] = newValue;

        m_fdnWriteIdx[c]++;
        if (m_fdnWriteIdx[c] >= MAX_FDN_DELAY_SAMPLES) {
            m_fdnWriteIdx[c] = 0;
        }
    }

    // 9. Downmix to stereo
    float wetL, wetR;
    MultiToStereo(fdnOutput, wetL, wetR);

    // 10. DC blocker
    float dcOutL = wetL - m_dcBlockerStateL;
    m_dcBlockerStateL = wetL - dcOutL * 0.995f;
    float dcOutR = wetR - m_dcBlockerStateR;
    m_dcBlockerStateR = wetR - dcOutR * 0.995f;

    // 11. Apply wet/dry mix
    outL = gainedL * m_dryGain + dcOutL * m_wetGain;
    outR = gainedR * m_dryGain + dcOutR * m_wetGain;
}

void GalaxyReverbModule::ProcessMono(float in) {
    BaseEffectModule::ProcessMono(in);

    UpdateParameters();

    float outL, outR;
    ProcessSample(in, in, outL, outR);

    m_audioLeft = outL;
    m_audioRight = outR;
}

void GalaxyReverbModule::ProcessStereo(float inL, float inR) {
    BaseEffectModule::ProcessStereo(inL, inR);

    UpdateParameters();

    float outL, outR;
    ProcessSample(inL, inR, outL, outR);

    m_audioLeft = outL;
    m_audioRight = outR;
}

float GalaxyReverbModule::GetBrightnessForLED(int led_id) const {
    float value = BaseEffectModule::GetBrightnessForLED(led_id);

    if (led_id == 1) {
        // Could pulse with decay or modulation
        return value;
    }

    return value;
}
