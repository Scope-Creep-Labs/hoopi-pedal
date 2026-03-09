/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef OUTPUT_PROCESSING_H
#define OUTPUT_PROCESSING_H

#include "daisy_seed.h"
#include "Delays/delayline_reverse.h"
#include "Delays/delayline_revoct.h"
#include "daisysp.h"
#include <cmath>

using namespace daisysp;

// Forward declarations from hoopi.h
extern uint8_t outDelayEnabled;
extern uint8_t outTremoloEnabled;
extern uint8_t outChorusEnabled;
extern uint8_t outDelayTime;
extern uint8_t outDelayFeedback;
extern uint8_t outDelayMix;
extern uint8_t outDelayType;
extern uint8_t outDelayTone;
extern uint8_t outTremoloRate;
extern uint8_t outTremoloDepth;
extern uint8_t outTremoloWave;
extern uint8_t outTremoloModRate;
extern uint8_t outChorusWet;
extern uint8_t outChorusDelay;
extern uint8_t outChorusLfoFreq;
extern uint8_t outChorusLfoDepth;
extern uint8_t outChorusFeedback;
extern bool outputProcessingEnabled;

/**
 * Output Processing Chain - Delay, Tremolo, Chorus
 * Applied AFTER all main effects, BEFORE output mixing
 * Parameters configurable via UART cmd=8 with effect_id=255 (global)
 *
 * Processing order: Delay -> Tremolo -> Chorus
 *
 * UART param_ids:
 *   - Delay enable: 40, params: 43-47
 *   - Tremolo enable: 41, params: 48-51
 *   - Chorus enable: 42, params: 52-56
 *   - Master enable: 57
 */

// SDRAM buffer sizes for 2 second stereo delay @ 48kHz
constexpr size_t OUTPUT_MAX_DELAY = static_cast<size_t>(48000.0f * 2.f);  // 2 seconds

// SDRAM buffers for stereo delay (declared in output_processing.h, defined once)
DelayLineRevOct<float, OUTPUT_MAX_DELAY> DSY_SDRAM_BSS outputDelayLineL;
DelayLineReverse<float, OUTPUT_MAX_DELAY> DSY_SDRAM_BSS outputDelayLineRevL;
DelayLineRevOct<float, OUTPUT_MAX_DELAY> DSY_SDRAM_BSS outputDelayLineR;
DelayLineReverse<float, OUTPUT_MAX_DELAY> DSY_SDRAM_BSS outputDelayLineRevR;

// Stereo delay struct (mirrors delayRevOct but for output processing)
struct StereoDelayLine {
    DelayLineRevOct<float, OUTPUT_MAX_DELAY> *del;
    DelayLineReverse<float, OUTPUT_MAX_DELAY> *delreverse;
    float currentDelay;
    float delayTarget;
    float feedback = 0.0f;
    bool active = true;
    bool reverseMode = false;
    Tone toneLP;
    float level = 1.0f;
    float level_reverse = 1.0f;
    bool dual_delay = false;

    float Process(float in) {
        fonepole(currentDelay, delayTarget, .0002f);
        del->SetDelay(currentDelay);
        delreverse->SetDelay1(currentDelay);

        float del_read = del->Read();
        float read_reverse = delreverse->ReadRev();
        float read = toneLP.Process(del_read);

        if (active) {
            del->Write((feedback * read) + in);
            delreverse->Write((feedback * read) + in);
        } else {
            del->Write(feedback * read);
            delreverse->Write(feedback * read);
        }

        if (dual_delay) {
            return read_reverse * level_reverse * 0.5f + read * level * 0.5f;
        } else if (reverseMode) {
            return read_reverse * level_reverse;
        } else {
            return read * level;
        }
    }
};

/**
 * Stereo Output Delay
 * 2 second max delay, stereo processing with same parameters for both channels
 */
class StereoOutputDelay {
public:
    void Init(float sample_rate) {
        m_sampleRate = sample_rate;
        m_delaySamplesMin = 2400.0f;   // 50ms min
        m_delaySamplesMax = sample_rate * 2.0f;  // 2 seconds max
        m_delayLpFreqMin = 300.0f;
        m_delayLpFreqMax = 20000.0f;

        // Initialize left channel delay
        outputDelayLineL.Init();
        outputDelayLineRevL.Init();
        m_delayL.del = &outputDelayLineL;
        m_delayL.delreverse = &outputDelayLineRevL;
        m_delayL.delayTarget = 24000;
        m_delayL.feedback = 0.0f;
        m_delayL.active = true;
        m_delayL.toneLP.Init(sample_rate);
        m_delayL.toneLP.SetFreq(20000.0f);

        // Initialize right channel delay
        outputDelayLineR.Init();
        outputDelayLineRevR.Init();
        m_delayR.del = &outputDelayLineR;
        m_delayR.delreverse = &outputDelayLineRevR;
        m_delayR.delayTarget = 24000;
        m_delayR.feedback = 0.0f;
        m_delayR.active = true;
        m_delayR.toneLP.Init(sample_rate);
        m_delayR.toneLP.SetFreq(20000.0f);

        CalculateDelayMix();
    }

    void CalculateDelayMix() {
        // Energy-constant crossfade from SignalSmith Blog
        float x2 = 1.0f - m_mixParam;
        float A = m_mixParam * x2;
        float B = A * (1.0f + 1.4186f * A);
        float C = B + m_mixParam;
        float D = B + x2;

        m_wetMix = C * C;
        m_dryMix = D * D;
    }

    void ProcessStereo(float& inL, float& inR) {
        // Set delay parameters for both channels
        float delaySamples = m_delaySamplesMin + (m_delaySamplesMax - m_delaySamplesMin) * m_timeParam;

        m_delayL.delayTarget = delaySamples;
        m_delayL.feedback = m_feedbackParam;
        m_delayL.reverseMode = m_reverseMode;
        m_delayL.dual_delay = m_dualMode;
        m_delayL.del->setOctave(m_octaveMode);

        m_delayR.delayTarget = delaySamples;
        m_delayR.feedback = m_feedbackParam;
        m_delayR.reverseMode = m_reverseMode;
        m_delayR.dual_delay = m_dualMode;
        m_delayR.del->setOctave(m_octaveMode);

        // Process both channels
        float delayOutL = m_delayL.Process(inL);
        float delayOutR = m_delayR.Process(inR);

        // Mix wet/dry
        inL = delayOutL * m_wetMix + inL * m_dryMix;
        inR = delayOutR * m_wetMix + inR * m_dryMix;
    }

    // Parameter setters from uint8_t (0-255)
    void SetTimeFromByte(uint8_t value) {
        m_timeParam = value / 255.0f;
    }

    void SetFeedbackFromByte(uint8_t value) {
        // 0-255 maps to 0-0.95 (prevent runaway feedback)
        m_feedbackParam = (value / 255.0f) * 0.95f;
    }

    void SetMixFromByte(uint8_t value) {
        m_mixParam = value / 255.0f;
        CalculateDelayMix();
    }

    void SetTypeFromByte(uint8_t value) {
        // 0 = Forward, 1 = Reverse, 2 = Octave, 3 = RevOct
        m_reverseMode = (value == 1 || value == 3);
        m_octaveMode = (value == 2 || value == 3);
        m_dualMode = (value == 3);
    }

    void SetToneFromByte(uint8_t value) {
        float freq = m_delayLpFreqMin + (value / 255.0f) * (m_delayLpFreqMax - m_delayLpFreqMin);
        m_delayL.toneLP.SetFreq(freq);
        m_delayR.toneLP.SetFreq(freq);
    }

private:
    float m_sampleRate;
    float m_delaySamplesMin;
    float m_delaySamplesMax;
    float m_delayLpFreqMin;
    float m_delayLpFreqMax;

    StereoDelayLine m_delayL;
    StereoDelayLine m_delayR;

    float m_timeParam = 0.5f;
    float m_feedbackParam = 0.3f;
    float m_mixParam = 0.3f;
    float m_wetMix = 0.3f;
    float m_dryMix = 0.7f;
    bool m_reverseMode = false;
    bool m_octaveMode = false;
    bool m_dualMode = false;
};

/**
 * Stereo Output Tremolo
 * Single modulator applied to both channels (same depth/rate)
 */
class StereoOutputTremolo {
public:
    void Init(float sample_rate) {
        m_sampleRate = sample_rate;
        m_tremolo.Init(sample_rate);
        m_freqOsc.Init(sample_rate);
        m_freqOsc.SetWaveform(Oscillator::WAVE_SIN);
        m_cachedMagnitude = 1.0f;
    }

    void ProcessStereo(float& inL, float& inR) {
        // Calculate tremolo frequency modulation (meta-modulation)
        m_freqOsc.SetAmp(0.5f);
        m_freqOsc.SetFreq(m_freqOscFreqMin + (m_modRateParam * m_freqOscFreqMax));
        float mod = 0.5f + m_freqOsc.Process();

        // If mod rate is off, no modulation
        if (m_modRateParam <= 0.01f) {
            mod = 1.0f;
        }

        // Set tremolo parameters
        m_tremolo.SetWaveform(m_waveform);
        m_tremolo.SetDepth(m_depthParam);
        m_tremolo.SetFreq(m_tremoloFreqMin + ((m_rateParam * m_tremoloFreqMax) * mod));

        // Smooth the tremolo value to avoid clicks with square waves
        fonepole(m_cachedMagnitude, m_tremolo.Process(1.0f), .01f);

        // Apply same modulation to both channels
        inL *= m_cachedMagnitude;
        inR *= m_cachedMagnitude;
    }

    // Parameter setters from uint8_t (0-255)
    void SetRateFromByte(uint8_t value) {
        m_rateParam = value / 255.0f;
    }

    void SetDepthFromByte(uint8_t value) {
        m_depthParam = value / 255.0f;
    }

    void SetWaveFromByte(uint8_t value) {
        // 0=Sine, 1=Triangle, 2=Saw, 3=Ramp, 4=Square
        m_waveform = (value > 4) ? 4 : value;
    }

    void SetModRateFromByte(uint8_t value) {
        m_modRateParam = value / 255.0f;
    }

private:
    float m_sampleRate;
    Tremolo m_tremolo;
    Oscillator m_freqOsc;

    float m_tremoloFreqMin = 1.0f;
    float m_tremoloFreqMax = 20.0f;
    float m_freqOscFreqMin = 0.01f;
    float m_freqOscFreqMax = 1.0f;

    float m_rateParam = 0.3f;
    float m_depthParam = 0.5f;
    int m_waveform = 0;
    float m_modRateParam = 0.0f;
    float m_cachedMagnitude = 1.0f;
};

/**
 * Stereo Output Chorus
 * DaisySP Chorus is already stereo - wrap it with byte parameter setters
 */
class StereoOutputChorus {
public:
    void Init(float sample_rate) {
        m_sampleRate = sample_rate;
        m_chorus.Init(sample_rate);
    }

    void ProcessStereo(float& inL, float& inR) {
        // Set chorus parameters
        m_chorus.SetDelay(m_delayParam);
        m_chorus.SetLfoFreq(m_lfoFreqMin + (m_lfoFreqParam * m_lfoFreqParam * (m_lfoFreqMax - m_lfoFreqMin)));
        m_chorus.SetLfoDepth(m_lfoDepthParam);
        m_chorus.SetFeedback(m_feedbackParam);

        // Process left channel through chorus
        m_chorus.Process(inL);

        // Get stereo output and mix with dry signal
        float wetL = m_chorus.GetLeft();
        float wetR = m_chorus.GetRight();

        inL = wetL * m_wetParam + inL * (1.0f - m_wetParam);
        inR = wetR * m_wetParam + inR * (1.0f - m_wetParam);
    }

    // Parameter setters from uint8_t (0-255)
    void SetWetFromByte(uint8_t value) {
        m_wetParam = value / 255.0f;
    }

    void SetDelayFromByte(uint8_t value) {
        m_delayParam = value / 255.0f;
    }

    void SetLfoFreqFromByte(uint8_t value) {
        m_lfoFreqParam = value / 255.0f;
    }

    void SetLfoDepthFromByte(uint8_t value) {
        m_lfoDepthParam = value / 255.0f;
    }

    void SetFeedbackFromByte(uint8_t value) {
        m_feedbackParam = value / 255.0f;
    }

private:
    float m_sampleRate;
    Chorus m_chorus;

    float m_lfoFreqMin = 1.0f;
    float m_lfoFreqMax = 20.0f;

    float m_wetParam = 0.65f;
    float m_delayParam = 0.5f;
    float m_lfoFreqParam = 0.25f;
    float m_lfoDepthParam = 0.3f;
    float m_feedbackParam = 0.25f;
};

/**
 * Output Processor - chains Delay -> Tremolo -> Chorus
 */
class OutputProcessor {
public:
    void Init(float sample_rate) {
        m_delay.Init(sample_rate);
        m_tremolo.Init(sample_rate);
        m_chorus.Init(sample_rate);

        m_delayEnabled = false;
        m_tremoloEnabled = false;
        m_chorusEnabled = false;

        UpdateFromGlobals();
    }

    void ProcessStereo(float& inL, float& inR) {
        // Processing order: Delay -> Tremolo -> Chorus
        if (m_delayEnabled) {
            m_delay.ProcessStereo(inL, inR);
        }
        if (m_tremoloEnabled) {
            m_tremolo.ProcessStereo(inL, inR);
        }
        if (m_chorusEnabled) {
            m_chorus.ProcessStereo(inL, inR);
        }
    }

    void SetDelayEnabled(bool enabled) { m_delayEnabled = enabled; }
    void SetTremoloEnabled(bool enabled) { m_tremoloEnabled = enabled; }
    void SetChorusEnabled(bool enabled) { m_chorusEnabled = enabled; }

    bool IsDelayEnabled() const { return m_delayEnabled; }
    bool IsTremoloEnabled() const { return m_tremoloEnabled; }
    bool IsChorusEnabled() const { return m_chorusEnabled; }

    StereoOutputDelay& GetDelay() { return m_delay; }
    StereoOutputTremolo& GetTremolo() { return m_tremolo; }
    StereoOutputChorus& GetChorus() { return m_chorus; }

    // Update all parameters from global variables
    void UpdateFromGlobals() {
        SetDelayEnabled(outDelayEnabled != 0);
        SetTremoloEnabled(outTremoloEnabled != 0);
        SetChorusEnabled(outChorusEnabled != 0);

        // Delay params
        m_delay.SetTimeFromByte(outDelayTime);
        m_delay.SetFeedbackFromByte(outDelayFeedback);
        m_delay.SetMixFromByte(outDelayMix);
        m_delay.SetTypeFromByte(outDelayType);
        m_delay.SetToneFromByte(outDelayTone);

        // Tremolo params
        m_tremolo.SetRateFromByte(outTremoloRate);
        m_tremolo.SetDepthFromByte(outTremoloDepth);
        m_tremolo.SetWaveFromByte(outTremoloWave);
        m_tremolo.SetModRateFromByte(outTremoloModRate);

        // Chorus params
        m_chorus.SetWetFromByte(outChorusWet);
        m_chorus.SetDelayFromByte(outChorusDelay);
        m_chorus.SetLfoFreqFromByte(outChorusLfoFreq);
        m_chorus.SetLfoDepthFromByte(outChorusLfoDepth);
        m_chorus.SetFeedbackFromByte(outChorusFeedback);
    }

private:
    StereoOutputDelay m_delay;
    StereoOutputTremolo m_tremolo;
    StereoOutputChorus m_chorus;
    bool m_delayEnabled;
    bool m_tremoloEnabled;
    bool m_chorusEnabled;
};

// Global instance
OutputProcessor outputProcessor;

void InitOutputProcessor(float samplerate) {
    outputProcessor.Init(samplerate);
}

// Update output processing based on toggle switch 2
// Mode 0 (Left): Clean - no output processing
// Mode 1 (Middle): All 3 effects enabled (for testing)
// Mode 2 (Right): Normal operation (use individual enable flags)
void UpdateOutputProcessorMode(int mode) {
    if (mode == 0) {
        // Clean mode - disable all output processing
        outputProcessor.SetDelayEnabled(false);
        outputProcessor.SetTremoloEnabled(false);
        outputProcessor.SetChorusEnabled(false);
    } else if (mode == 1) {
        // Test mode - enable all 3 with defaults
        outputProcessor.SetDelayEnabled(true);
        outputProcessor.SetTremoloEnabled(true);
        outputProcessor.SetChorusEnabled(true);
    }
    // Mode 2: Leave as-is (controlled by UART params)
}

// Process output chain: Delay -> Tremolo -> Chorus
inline void ProcessOutputChain(float& inL, float& inR) {
    if (!outputProcessingEnabled) return;
    outputProcessor.ProcessStereo(inL, inR);
}

/**
 * Handle UART parameter for output processing (param_id 40-57)
 * Updates global vars and applies to outputProcessor
 * @return true if param was handled, false otherwise
 */
inline bool HandleOutputParam(uint8_t paramId, uint8_t value) {
    switch (paramId) {
        // Enable flags (40-42)
        case 40:  // Delay Enable
            outDelayEnabled = value;
            outputProcessor.SetDelayEnabled(value != 0);
            return true;
        case 41:  // Tremolo Enable
            outTremoloEnabled = value;
            outputProcessor.SetTremoloEnabled(value != 0);
            return true;
        case 42:  // Chorus Enable
            outChorusEnabled = value;
            outputProcessor.SetChorusEnabled(value != 0);
            return true;

        // Delay params (43-47)
        case 43:  // Delay Time
            outDelayTime = value;
            outputProcessor.GetDelay().SetTimeFromByte(value);
            return true;
        case 44:  // Delay Feedback
            outDelayFeedback = value;
            outputProcessor.GetDelay().SetFeedbackFromByte(value);
            return true;
        case 45:  // Delay Mix
            outDelayMix = value;
            outputProcessor.GetDelay().SetMixFromByte(value);
            return true;
        case 46:  // Delay Type
            outDelayType = value;
            outputProcessor.GetDelay().SetTypeFromByte(value);
            return true;
        case 47:  // Delay Tone
            outDelayTone = value;
            outputProcessor.GetDelay().SetToneFromByte(value);
            return true;

        // Tremolo params (48-51)
        case 48:  // Tremolo Rate
            outTremoloRate = value;
            outputProcessor.GetTremolo().SetRateFromByte(value);
            return true;
        case 49:  // Tremolo Depth
            outTremoloDepth = value;
            outputProcessor.GetTremolo().SetDepthFromByte(value);
            return true;
        case 50:  // Tremolo Wave
            outTremoloWave = value;
            outputProcessor.GetTremolo().SetWaveFromByte(value);
            return true;
        case 51:  // Tremolo Mod Rate
            outTremoloModRate = value;
            outputProcessor.GetTremolo().SetModRateFromByte(value);
            return true;

        // Chorus params (52-56)
        case 52:  // Chorus Wet
            outChorusWet = value;
            outputProcessor.GetChorus().SetWetFromByte(value);
            return true;
        case 53:  // Chorus Delay
            outChorusDelay = value;
            outputProcessor.GetChorus().SetDelayFromByte(value);
            return true;
        case 54:  // Chorus LFO Freq
            outChorusLfoFreq = value;
            outputProcessor.GetChorus().SetLfoFreqFromByte(value);
            return true;
        case 55:  // Chorus LFO Depth
            outChorusLfoDepth = value;
            outputProcessor.GetChorus().SetLfoDepthFromByte(value);
            return true;
        case 56:  // Chorus Feedback
            outChorusFeedback = value;
            outputProcessor.GetChorus().SetFeedbackFromByte(value);
            return true;

        // Master enable (57)
        case 57:
            outputProcessingEnabled = (value != 0);
            return true;

        default:
            return false;
    }
}

#endif /* OUTPUT_PROCESSING_H */
