/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "nam_module.h"
#include "nam_wavenet.h"
#include "../nam_models.h"
#include <q/fx/biquad.hpp>

using namespace bkshepherd;

// NamWavenet instance - placed in D2 RAM for faster access
static NamWavenet DSY_SDRAM_BSS namWavenet;

// 3-band EQ
constexpr uint8_t NUM_FILTERS_NAM = 3;
const float minGain = -10.f;
const float maxGain = 10.f;
const float centerFrequencyNam[NUM_FILTERS_NAM] = {110.f, 900.f, 4000.f};
const float q_nam[NUM_FILTERS_NAM] = {.7f, .7f, .7f};

cycfi::q::peaking filter_nam[NUM_FILTERS_NAM] = {
    {0, centerFrequencyNam[0], 48000, q_nam[0]},
    {0, centerFrequencyNam[1], 48000, q_nam[1]},
    {0, centerFrequencyNam[2], 48000, q_nam[2]}
};

static const int s_paramCount = 8;
static const ParameterMetaData s_metaData[s_paramCount] = {
    {
        name : "Gain",
        valueType : ParameterValueType::Float,
        valueCurve : ParameterValueCurve::Log,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 0,
        midiCCMapping : 14,
    },
    {name : "Level", valueType : ParameterValueType::Float, defaultValue : {.float_value = 0.5f}, knobMapping : 1, midiCCMapping : 15},
    {
        name : "Model",
        valueType : ParameterValueType::Binned,
        valueBinCount : 8,  // 8 NAM Nano models
        valueBinNames : nullptr,
        defaultValue : {.uint_value = 0},
        knobMapping : 2,
        midiCCMapping : 16
    },
    {
        name : "Bass",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.0f},
        knobMapping : 3,
        midiCCMapping : 17,
        minValue : static_cast<int>(minGain),
        maxValue : static_cast<int>(maxGain)
    },
    {
        name : "Mid",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.0f},
        knobMapping : 4,
        midiCCMapping : 18,
        minValue : static_cast<int>(minGain),
        maxValue : static_cast<int>(maxGain)
    },
    {
        name : "Treble",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.0f},
        knobMapping : 5,
        midiCCMapping : 19,
        minValue : static_cast<int>(minGain),
        maxValue : static_cast<int>(maxGain)
    },
    {
        name : "NeuralModel",
        valueType : ParameterValueType::Bool,
        valueBinCount : 0,
        defaultValue : {.uint_value = 1},
        knobMapping : -1,
        midiCCMapping : 20
    },
    {
        name : "EQ",
        valueType : ParameterValueType::Bool,
        valueBinCount : 0,
        defaultValue : {.uint_value = 1},
        knobMapping : -1,
        midiCCMapping : 21
    },
};

// Default Constructor
NamModule::NamModule()
    : BaseEffectModule(), m_gainMin(0.0f), m_gainMax(2.0f), m_levelMin(0.0f), m_levelMax(2.0f), m_cachedEffectMagnitudeValue(1.0f) {
    m_name = "NAM";
    m_paramMetaData = s_metaData;
    this->InitParams(s_paramCount);
}

// Destructor
NamModule::~NamModule() {
}

void NamModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);

    // Load the model based on current knob position
    // Force initial load by setting m_currentModelindex to invalid value
    m_currentModelindex = -1;
    SelectModel();

    // Initialize EQ filters
    filter_nam[0].config(GetParameterAsFloat(3), centerFrequencyNam[0], sample_rate, q_nam[0]);
    filter_nam[1].config(GetParameterAsFloat(4), centerFrequencyNam[1], sample_rate, q_nam[1]);
    filter_nam[2].config(GetParameterAsFloat(5), centerFrequencyNam[2], sample_rate, q_nam[2]);
}

void NamModule::ParameterChanged(int parameter_id) {
    if (parameter_id == 2) {
        SelectModel();
    } else if (parameter_id == 3) {
        filter_nam[0].config(GetParameterAsFloat(3), centerFrequencyNam[0], GetSampleRate(), q_nam[0]);
    } else if (parameter_id == 4) {
        filter_nam[1].config(GetParameterAsFloat(4), centerFrequencyNam[1], GetSampleRate(), q_nam[1]);
    } else if (parameter_id == 5) {
        filter_nam[2].config(GetParameterAsFloat(5), centerFrequencyNam[2], GetSampleRate(), q_nam[2]);
    }
}

void NamModule::SelectModel() {
    const int modelIndex = GetParameterAsBinnedValue(2) - 1;

    if (m_currentModelindex != modelIndex && modelIndex >= 0 && modelIndex < NAM_MODEL_COUNT) {
        m_muteOutput = true;
        const float* weights = GetModelWeights(modelIndex);
        if (weights != nullptr) {
            namWavenet.load_weights(weights);
            namWavenet.reset();  // Clear state buffers for clean transition
            m_currentModelindex = modelIndex;
        }
        m_muteOutput = false;
    }
}

void NamModule::ProcessBlock(const float* in, float* out, size_t size) {
    if (m_muteOutput) {
        for (size_t i = 0; i < size; i++) {
            out[i] = 0.0f;
        }
        return;
    }

    const float gain = m_gainMin + (m_gainMax - m_gainMin) * GetParameterAsFloat(0);
    const float level = m_levelMin + (GetParameterAsFloat(1) * (m_levelMax - m_levelMin));
    const bool neuralEnabled = GetParameterAsBool(6);
    const bool eqEnabled = GetParameterAsBool(7);

    // Apply input gain
    static float inputBuf[NAM_BLOCK];
    for (size_t i = 0; i < size && i < NAM_BLOCK; i++) {
        inputBuf[i] = in[i] * gain;
    }

    // Process through neural network
    if (neuralEnabled) {
        namWavenet.forward(inputBuf, out);

        // Apply head scale and output level
        for (size_t i = 0; i < size && i < NAM_BLOCK; i++) {
            out[i] *= 0.4f * level;  // 0.4 = output normalization factor
        }
    } else {
        // Bypass neural network
        for (size_t i = 0; i < size && i < NAM_BLOCK; i++) {
            out[i] = inputBuf[i] * level;
        }
    }

    // Apply 3-band EQ
    if (eqEnabled) {
        for (size_t i = 0; i < size && i < NAM_BLOCK; i++) {
            float sample = out[i];
            for (uint8_t f = 0; f < NUM_FILTERS_NAM; f++) {
                sample = filter_nam[f](sample);
            }
            out[i] = sample;
        }
    }
}

// Legacy single-sample processing (not recommended for NAM)
void NamModule::ProcessMono(float in) {
    // For single-sample processing, just pass through
    // Block processing should be used instead
    m_audioLeft = m_audioRight = in;
}

void NamModule::ProcessStereo(float inL, float inR) {
    ProcessMono(inL);
}

float NamModule::GetBrightnessForLED(int led_id) const {
    float value = BaseEffectModule::GetBrightnessForLED(led_id);

    if (led_id == 1) {
        return value * m_cachedEffectMagnitudeValue;
    }

    return value;
}
