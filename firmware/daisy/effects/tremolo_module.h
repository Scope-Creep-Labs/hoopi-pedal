/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef TREMOLO_MODULE_H
#define TREMOLO_MODULE_H

#include "base_effect_module.h"
#include "daisysp.h"
#include <stdint.h>
#ifdef __cplusplus

using namespace daisysp;

namespace bkshepherd {

class TremoloModule : public BaseEffectModule {
  public:
    TremoloModule();
    ~TremoloModule();

    void Init(float sample_rate) override;
    void ProcessMono(float in) override;
    void ProcessStereo(float inL, float inR) override;
    float GetBrightnessForLED(int led_id) const override;

  private:
    daisysp::Tremolo m_tremolo;
    float m_tremoloFreqMin;
    float m_tremoloFreqMax;

    Oscillator m_freqOsc;
    float m_freqOscFreqMin;
    float m_freqOscFreqMax;

    float m_cachedEffectMagnitudeValue;
};
} // namespace bkshepherd
#endif
#endif
