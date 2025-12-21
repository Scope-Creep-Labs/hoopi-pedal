/**
 * Hoopi Pedal - Effects module
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "tremolo_module.h"

using namespace bkshepherd;

static const char *s_waveBinNames[5] = {"Sine", "Triangle", "Saw", "Ramp", "Square"};

static const int s_paramCount = 4;
static const ParameterMetaData s_metaData[s_paramCount] = {
    {
        name : "Rate",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.3f},
        knobMapping : 0,
        midiCCMapping : 14
    },
    {
        name : "Depth",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 1,
        midiCCMapping : 15
    },
    {
        name : "Wave",
        valueType : ParameterValueType::Binned,
        valueBinCount : 5,
        valueBinNames : s_waveBinNames,
        defaultValue : {.uint_value = 0},
        knobMapping : 2,
        midiCCMapping : 16
    },
    {
        name : "Mod Rate",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.0f},
        knobMapping : 3,
        midiCCMapping : 17
    }
};

TremoloModule::TremoloModule()
    : BaseEffectModule(), m_tremoloFreqMin(1.0f), m_tremoloFreqMax(20.0f),
      m_freqOscFreqMin(0.01f), m_freqOscFreqMax(1.0f),
      m_cachedEffectMagnitudeValue(1.0f) {
    m_name = "Tremolo";
    m_paramMetaData = s_metaData;
    this->InitParams(s_paramCount);
}

TremoloModule::~TremoloModule() {}

void TremoloModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);
    m_tremolo.Init(sample_rate);
    m_freqOsc.Init(sample_rate);
    m_freqOsc.SetWaveform(Oscillator::WAVE_SIN);
}

void TremoloModule::ProcessMono(float in) {
    BaseEffectModule::ProcessMono(in);

    // Calculate Tremolo Frequency Modulation
    float modRate = GetParameterAsFloat(3);
    m_freqOsc.SetAmp(0.5f);
    m_freqOsc.SetFreq(m_freqOscFreqMin + (modRate * m_freqOscFreqMax));
    float mod = 0.5f + m_freqOsc.Process();

    // If mod rate is off, no modulation
    if (modRate <= 0.01f) {
        mod = 1.0f;
    }

    // Set tremolo parameters
    m_tremolo.SetWaveform(GetParameterAsBinnedValue(2) - 1);
    m_tremolo.SetDepth(GetParameterAsFloat(1));
    m_tremolo.SetFreq(m_tremoloFreqMin + ((GetParameterAsFloat(0) * m_tremoloFreqMax) * mod));

    // Smooth the tremolo value to avoid clicks with square waves
    fonepole(m_cachedEffectMagnitudeValue, m_tremolo.Process(1.0f), .01f);

    m_audioLeft = m_audioLeft * m_cachedEffectMagnitudeValue;
    m_audioRight = m_audioLeft;
}

void TremoloModule::ProcessStereo(float inL, float inR) {
    ProcessMono(inL);
}

float TremoloModule::GetBrightnessForLED(int led_id) const {
    float value = BaseEffectModule::GetBrightnessForLED(led_id);

    if (led_id == 1) {
        return value * m_cachedEffectMagnitudeValue;
    }

    return value;
}
