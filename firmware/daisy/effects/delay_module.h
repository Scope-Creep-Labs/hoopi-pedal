/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef DELAY_MODULE_H
#define DELAY_MODULE_H

#include "Delays/delayline_reverse.h"
#include "Delays/delayline_revoct.h"
#include "base_effect_module.h"
#include "daisysp.h"
#include <stdint.h>
#ifdef __cplusplus

using namespace daisysp;

// Delay Max Definitions (Assumes 48kHz samplerate)
constexpr size_t MAX_DELAY_NORM =
    static_cast<size_t>(48000.0f * 8.f); // 8 second max delay
constexpr size_t MAX_DELAY_REV =
    static_cast<size_t>(48000.0f * 8.f); // 8 second max delay (double for reverse)
constexpr size_t MAX_DELAY_SPREAD = static_cast<size_t>(4800.0f); // 50ms for Spread

// Core delay struct with forward/reverse/octave capabilities
struct delayRevOct {
    DelayLineRevOct<float, MAX_DELAY_NORM> *del;
    DelayLineReverse<float, MAX_DELAY_REV> *delreverse;
    float currentDelay;
    float delayTarget;
    float feedback = 0.0;
    float active = false;
    bool reverseMode = false;
    Tone toneOctLP;
    float level = 1.0;
    float level_reverse = 1.0;
    bool dual_delay = false;
    bool secondTapOn = false;

    float Process(float in) {
        fonepole(currentDelay, delayTarget, .0002f);
        del->SetDelay(currentDelay);
        delreverse->SetDelay1(currentDelay);

        float del_read = del->Read();
        float read_reverse = delreverse->ReadRev();
        float read = toneOctLP.Process(del_read);

        float secondTap = 0.0;
        if (secondTapOn) {
            secondTap = del->ReadSecondTap();
        }

        if (active) {
            del->Write((feedback * read) + in);
            delreverse->Write((feedback * read) + in);
        } else {
            del->Write(feedback * read);
            delreverse->Write(feedback * read);
        }

        if (dual_delay) {
            return read_reverse * level_reverse * 0.5 + (read + secondTap) * level * 0.5;
        } else if (reverseMode) {
            return read_reverse * level_reverse;
        } else {
            return (read + secondTap) * level;
        }
    }
};

namespace bkshepherd {

class DelayModule : public BaseEffectModule {
  public:
    DelayModule();
    ~DelayModule();

    void Init(float sample_rate) override;
    void UpdateLEDRate();
    void CalculateDelayMix();
    void ParameterChanged(int parameter_id) override;
    void ProcessMono(float in) override;
    void ProcessStereo(float inL, float inR) override;
    void SetTempo(uint32_t bpm) override;
    float GetBrightnessForLED(int led_id) const override;

  private:
    float m_delaylpFreqMin;
    float m_delaylpFreqMax;
    float m_delaySamplesMin;
    float m_delaySamplesMax;
    float m_LEDValue;

    delayRevOct delayLeft;

    // Mix params
    float delayWetMix = 0.5;
    float delayDryMix = 0.5;

    float effect_samplerate;

    Oscillator led_osc;
};
} // namespace bkshepherd
#endif
#endif
