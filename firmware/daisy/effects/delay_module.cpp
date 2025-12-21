/**
 * Based on bkshepherd/DaisySeedProjects
 * https://github.com/bkshepherd/DaisySeedProjects/blob/main/Software/GuitarPedal/Effect-Modules
 */

#include "delay_module.h"

using namespace bkshepherd;

// Inline tempo conversion
static inline float tempo_to_freq(uint32_t tempo) { return static_cast<float>(tempo) / 60.0f; }

static const char *s_delayTypes[4] = {"Forward", "Reverse", "Octave", "RevOct"};

// SDRAM buffers for delay lines
DelayLineRevOct<float, MAX_DELAY_NORM> DSY_SDRAM_BSS delayLineLeft;
DelayLineReverse<float, MAX_DELAY_REV> DSY_SDRAM_BSS delayLineRevLeft;

static const int s_paramCount = 5;
static const ParameterMetaData s_metaData[s_paramCount] = {
    {
        name : "Delay Time",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 0,
        midiCCMapping : 14
    },
    {
        name : "Feedback",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 1,
        midiCCMapping : 15
    },
    {
        name : "Mix",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 2,
        midiCCMapping : 16
    },
    {
        name : "Type",
        valueType : ParameterValueType::Binned,
        valueBinCount : 4,
        valueBinNames : s_delayTypes,
        defaultValue : {.uint_value = 0},
        knobMapping : 3,
        midiCCMapping : 17
    },
    {
        name : "Tone",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 1.0f},
        knobMapping : -1,
        midiCCMapping : 18
    }
};

DelayModule::DelayModule()
    : BaseEffectModule(), m_delaylpFreqMin(300.0f), m_delaylpFreqMax(20000.0f),
      m_delaySamplesMin(2400.0f), m_delaySamplesMax(192000.0f), m_LEDValue(1.0f) {
    m_name = "Delay";
    m_paramMetaData = s_metaData;
    this->InitParams(s_paramCount);
}

DelayModule::~DelayModule() {}

void DelayModule::UpdateLEDRate() {
    float timeParam = GetParameterAsFloat(0);
    float delaySamples = m_delaySamplesMin + (m_delaySamplesMax - m_delaySamplesMin) * timeParam;
    float delayFreq = effect_samplerate / delaySamples;
    led_osc.SetFreq(delayFreq / 2.0);
}

void DelayModule::CalculateDelayMix() {
    // Energy-constant crossfade from SignalSmith Blog
    float delMixKnob = GetParameterAsFloat(2);
    float x2 = 1.0 - delMixKnob;
    float A = delMixKnob * x2;
    float B = A * (1.0 + 1.4186 * A);
    float C = B + delMixKnob;
    float D = B + x2;

    delayWetMix = C * C;
    delayDryMix = D * D;
}

void DelayModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);

    delayLineLeft.Init();
    delayLineRevLeft.Init();
    delayLeft.del = &delayLineLeft;
    delayLeft.delreverse = &delayLineRevLeft;
    delayLeft.delayTarget = 24000;
    delayLeft.feedback = 0.0;
    delayLeft.active = true;
    delayLeft.toneOctLP.Init(sample_rate);
    delayLeft.toneOctLP.SetFreq(20000.0);

    effect_samplerate = sample_rate;

    led_osc.Init(sample_rate);
    led_osc.SetWaveform(1);
    led_osc.SetFreq(2.0);

    CalculateDelayMix();
}

void DelayModule::ParameterChanged(int parameter_id) {
    if (parameter_id == 0) {
        UpdateLEDRate();
    } else if (parameter_id == 2) {
        CalculateDelayMix();
    } else if (parameter_id == 4) {
        delayLeft.toneOctLP.SetFreq(m_delaylpFreqMin + (m_delaylpFreqMax - m_delaylpFreqMin) * GetParameterAsFloat(4));
    }
}

void DelayModule::ProcessMono(float in) {
    BaseEffectModule::ProcessMono(in);

    m_LEDValue = led_osc.Process();

    int delayType = GetParameterAsBinnedValue(3) - 1;
    float timeParam = GetParameterAsFloat(0);

    delayLeft.delayTarget = m_delaySamplesMin + (m_delaySamplesMax - m_delaySamplesMin) * timeParam;
    delayLeft.feedback = GetParameterAsFloat(1);

    // Reverse mode: types 1 (Reverse) and 3 (RevOct)
    delayLeft.reverseMode = (delayType == 1 || delayType == 3);

    // Octave mode: types 2 (Octave) and 3 (RevOct)
    delayLeft.del->setOctave(delayType == 2 || delayType == 3);

    float delLeft_out = delayLeft.Process(m_audioLeft);

    m_audioLeft = delLeft_out * delayWetMix + m_audioLeft * delayDryMix;
    m_audioRight = m_audioLeft;
}

void DelayModule::ProcessStereo(float inL, float inR) {
    ProcessMono(inL);
}

void DelayModule::SetTempo(uint32_t bpm) {
    float freq = tempo_to_freq(bpm);
    float delay_in_samples = effect_samplerate / freq;

    if (delay_in_samples <= m_delaySamplesMin) {
        SetParameterAsMagnitude(0, 0.0f);
    } else if (delay_in_samples >= m_delaySamplesMax) {
        SetParameterAsMagnitude(0, 1.0f);
    } else {
        float magnitude = static_cast<float>(delay_in_samples - m_delaySamplesMin) /
                          static_cast<float>(m_delaySamplesMax - m_delaySamplesMin);
        SetParameterAsMagnitude(0, magnitude);
    }
    UpdateLEDRate();
}

float DelayModule::GetBrightnessForLED(int led_id) const {
    float value = BaseEffectModule::GetBrightnessForLED(led_id);

    float ledValue = (m_LEDValue > 0.45) ? 1.0 : 0.0;

    if (led_id == 1) {
        return value * ledValue;
    }

    return value;
}
