/**
 * Based on bkshepherd/DaisySeedProjects
 * https://github.com/bkshepherd/DaisySeedProjects/blob/main/Software/GuitarPedal/Effect-Modules
 */

#include "cloudseed_module.h"

// This is used in the modified CloudSeed code for allocating
// delay line memory to SDRAM (64MB available on Daisy)
#define CUSTOM_POOL_SIZE (48 * 384 * 384)

DSY_SDRAM_BSS char custom_pool[CUSTOM_POOL_SIZE];
size_t pool_index = 0;
int allocation_count = 0;
void *custom_pool_allocate(size_t size) {
    if (pool_index + size >= CUSTOM_POOL_SIZE) {
        return 0;
    }
    void *ptr = &custom_pool[pool_index];
    pool_index += size;
    return ptr;
}

using namespace bkshepherd;

static const char *s_presetNames[8] = {"FChorus", "DullEchos", "Hyperplane", "MedSpace", "Hallway", "RubiKa", "SmallRoom", "90s"};

// Knob layout: PreDelay, Decay, In Gain L, Tone, Mix, In Gain R
// UART-only: Mod Amt, Mod Rate, Preset
static const int s_paramCount = 12;
static ParameterMetaData s_metaData[s_paramCount] = {
    {
        name : "PreDelay",
        valueType : ParameterValueType::Float,
        defaultValue : {.float_value = 0.0f},
        knobMapping : 0,
        midiCCMapping : 14
    },
    {name : "Mix", valueType : ParameterValueType::Float, defaultValue : {.float_value = 0.5f}, knobMapping : 4, midiCCMapping : 15},
    {name : "Decay", valueType : ParameterValueType::Float, defaultValue : {.float_value = 0.5f}, knobMapping : 1, midiCCMapping : 16},
    {
        name : "Mod Amt",
        valueType : ParameterValueType::Float,
        defaultValue : {.float_value = 0.5f},
        knobMapping : -1,  // UART only
        midiCCMapping : 17
    },
    {
        name : "Mod Rate",
        valueType : ParameterValueType::Float,
        defaultValue : {.float_value = 0.5f},
        knobMapping : -1,  // UART only
        midiCCMapping : 18
    },
    {name : "Tone", valueType : ParameterValueType::Float, defaultValue : {.float_value = 0.5f}, knobMapping : 3, midiCCMapping : 19},
    {
        name : "Preset",
        valueType : ParameterValueType::Binned,
        valueBinCount : 8,
        valueBinNames : s_presetNames,
        defaultValue : {.uint_value = 6},
        knobMapping : -1,
        midiCCMapping : 20
    },
    {name : "Sum2Mono", valueType : ParameterValueType::Bool, defaultValue : {.uint_value = 0}, knobMapping : -1, midiCCMapping : -1},
    {name : "StereoIn", valueType : ParameterValueType::Bool, defaultValue : {.uint_value = 1}, knobMapping : -1, midiCCMapping : -1},
    {name : "DryVolume", valueType : ParameterValueType::Float, defaultValue : {.float_value = 0.5f}, knobMapping : -1, midiCCMapping : -1},
    {name : "In Gain L", valueType : ParameterValueType::Float, defaultValue : {.float_value = 0.0f}, knobMapping : 2, midiCCMapping : 21, minValue : 1, maxValue : 20},
    {name : "In Gain R", valueType : ParameterValueType::Float, defaultValue : {.float_value = 0.0f}, knobMapping : 5, midiCCMapping : 22, minValue : 1, maxValue : 20}
};

// Default Constructor
CloudSeedModule::CloudSeedModule() : BaseEffectModule(), m_gainMin(0.0f), m_gainMax(1.0f), m_cachedEffectMagnitudeValue(1.0f) {
    // Set the name of the effect
    m_name = "CloudSeed";

    // Setup the meta data reference for this Effect
    m_paramMetaData = s_metaData;

    // Initialize Parameters for this Effect
    this->InitParams(s_paramCount);
}

// Destructor
CloudSeedModule::~CloudSeedModule() {
    // No Code Needed
}

void CloudSeedModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);

    AudioLib::ValueTables::Init();
    CloudSeed::FastSin::Init();

    reverb = new CloudSeed::ReverbController(sample_rate);
    reverb->ClearBuffers();
    reverb->initFactorySmallRoom();  // Start with lighter preset
    reverb->SetParameter(::Parameter2::LineCount, 2);  // 3 can freeze, 2 is safe
    CalculateMix();
}

void CloudSeedModule::ParameterChanged(int parameter_id) {
    if (parameter_id == 6) { // Preset
        changePreset();
    } else {
        if (throttle_counter > 5) {
            if (parameter_id == 0) {
                reverb->SetParameter(::Parameter2::PreDelay, GetParameterAsFloat(0) * 0.95);
            } else if (parameter_id == 1) {
                if (!inputMuteForWet) {
                    linearChangeDryLevel.deactivate();
                    CalculateMix();
                } else {
                    currentMix.wet = CalculateMix(GetParameterAsFloat(1)).wet;
                }
            } else if (parameter_id == 2) {
                reverb->SetParameter(::Parameter2::LineDecay, GetParameterAsFloat(2));
            } else if (parameter_id == 3) {
                reverb->SetParameter(::Parameter2::LineModAmount, GetParameterAsFloat(3));
            } else if (parameter_id == 4) {
                reverb->SetParameter(::Parameter2::LineModRate, GetParameterAsFloat(4));
            } else if (parameter_id == 5) {
                reverb->SetParameter(::Parameter2::CutoffEnabled, 1.0);
                reverb->SetParameter(::Parameter2::PostCutoffFrequency, GetParameterAsFloat(5));
            } else if (parameter_id == 9) {  // DryVolume
                if (inputMuteForWet) {
                    linearChangeDryLevel.deactivate();
                    currentMix.dry = GetParameterAsFloat(9);
                }
            }
            throttle_counter = 0;
        }
        throttle_counter += 1;
    }
}

void CloudSeedModule::AlternateFootswitchPressed() {
    inputMuteForWet = !inputMuteForWet;

    if (inputMuteForWet) {
        linearChangeDryLevel.activate(currentMix.dry, GetParameterAsFloat(9), linearChangeDryLevelSteps);
        s_metaData[9].knobMapping = s_metaData[1].knobMapping;
        s_metaData[1].knobMapping = -1;
    } else {
        linearChangeDryLevel.activate(currentMix.dry, CalculateMix(GetParameterAsFloat(1)).dry, linearChangeDryLevelSteps);
        s_metaData[1].knobMapping = s_metaData[9].knobMapping;
        s_metaData[9].knobMapping = -1;
    }
}

void CloudSeedModule::changePreset() {
    int c = (GetParameterAsBinnedValue(6) - 1);
    reverb->ClearBuffers();

    if (c == 0) {
        reverb->initFactoryChorus();
    } else if (c == 1) {
        reverb->initFactoryDullEchos();
    } else if (c == 2) {
        reverb->initFactoryHyperplane();
    } else if (c == 3) {
        reverb->initFactoryMediumSpace();
    } else if (c == 4) {
        reverb->initFactoryNoiseInTheHallway();
    } else if (c == 5) {
        reverb->initFactoryRubiKaFields();
    } else if (c == 6) {
        reverb->initFactorySmallRoom();
    } else if (c == 7) {
        reverb->initFactory90sAreBack();
    }
}

void CloudSeedModule::CalculateMix() {
    currentMix = CalculateMix(GetParameterAsFloat(1));
}

CloudSeedModule::Mix CloudSeedModule::CalculateMix(float mixValue) {
    // A computationally cheap mostly energy constant crossfade from SignalSmith Blog
    // https://signalsmith-audio.co.uk/writing/2021/cheap-energy-crossfade/
    float x2 = 1.0 - mixValue;
    float A = mixValue * x2;
    float B = A * (1.0 + 1.4186 * A);
    float C = B + mixValue;
    float D = B + x2;

    float wetMix = C * C;
    float dryMix = D * D;
    return Mix{wetMix, dryMix};
}

static float inMuted[1] = {0};

void CloudSeedModule::ProcessMono(float in) {
    BaseEffectModule::ProcessMono(in);

    // Apply input gain L (param 10) - 0-1 maps to 1-20x
    float inputGainL = 1.0f + GetParameterAsFloat(10) * 19.0f;

    float inL[1];
    float outL[1];
    float inR[1];
    float outR[1];

    inL[0] = m_audioLeft * inputGainL;
    inR[0] = m_audioLeft * inputGainL;

    if (inputMuteForWet) {
        reverb->Process(inMuted, inMuted, outL, outR, 1);
    } else {
        reverb->Process(inL, inR, outL, outR, 1);
    }

    if (linearChangeDryLevel.isActive()) {
        currentMix.dry = linearChangeDryLevel.getNextValue();
    }

    if (GetParameterAsBool(7)) {
        m_audioLeft = ((outL[0] + outR[0]) / 2.0) * currentMix.wet + inL[0] * currentMix.dry;
        m_audioRight = m_audioLeft;
    } else {
        m_audioLeft = outL[0] * currentMix.wet + inL[0] * currentMix.dry;
        m_audioRight = outR[0] * currentMix.wet + inR[0] * currentMix.dry;
    }
}

void CloudSeedModule::ProcessStereo(float inL, float inR) {
    BaseEffectModule::ProcessStereo(inL, inR);

    // Apply input gains (param 10 = L, param 11 = R) - 0-1 maps to 1-20x
    float inputGainL = 1.0f + GetParameterAsFloat(10) * 19.0f;
    float inputGainR = 1.0f + GetParameterAsFloat(11) * 19.0f;

    float inL2[1];
    float outL[1];
    float inR2[1];
    float outR[1];

    inL2[0] = m_audioLeft * inputGainL;
    if (GetParameterAsBool(8)) {
        inR2[0] = m_audioRight * inputGainR;
    } else {
        inR2[0] = m_audioLeft * inputGainL;
    }

    if (inputMuteForWet) {
        reverb->Process(inMuted, inMuted, outL, outR, 1);
    } else {
        reverb->Process(inL2, inR2, outL, outR, 1);
    }

    if (linearChangeDryLevel.isActive()) {
        currentMix.dry = linearChangeDryLevel.getNextValue();
    }

    // For dry mix, use the gained input
    if (GetParameterAsBool(7)) {
        m_audioLeft = ((outL[0] + outR[0]) / 2.0) * currentMix.wet + inL2[0] * currentMix.dry;
        m_audioRight = m_audioLeft;
    } else {
        m_audioLeft = outL[0] * currentMix.wet + inL2[0] * currentMix.dry;
        m_audioRight = outR[0] * currentMix.wet + inR2[0] * currentMix.dry;
    }
}

float CloudSeedModule::GetBrightnessForLED(int led_id) const {
    float value = BaseEffectModule::GetBrightnessForLED(led_id);

    static long flashCounter = 0;

    if (led_id == 1) {
        if (inputMuteForWet) {
            flashCounter++;
            if ((flashCounter / 10000) % 2 == 0) {
                return value * m_cachedEffectMagnitudeValue;
            } else {
                return 0;
            }
        }
        return value * m_cachedEffectMagnitudeValue;
    }

    return value;
}
