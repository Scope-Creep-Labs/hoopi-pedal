/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef NAM_MODULE_H
#define NAM_MODULE_H

#include "base_effect_module.h"
#include "nam_wavenet.h"
#include <stdint.h>

#ifdef __cplusplus

/** @file nam_module.h */

namespace bkshepherd {

class NamModule : public BaseEffectModule {
  public:
    NamModule();
    ~NamModule();

    void Init(float sample_rate) override;
    void ParameterChanged(int parameter_id) override;
    void SelectModel();

    // Block processing for NAM (256 samples at a time)
    void ProcessBlock(const float* in, float* out, size_t size);

    // Legacy single-sample interface (not recommended for NAM)
    void ProcessMono(float in) override;
    void ProcessStereo(float inL, float inR) override;
    float GetBrightnessForLED(int led_id) const override;

  private:
    float m_gainMin;
    float m_gainMax;

    float m_levelMin;
    float m_levelMax;

    int m_currentModelindex = -1;

    float m_cachedEffectMagnitudeValue;

    // Used for nicely switching models
    bool m_muteOutput = false;
};
} // namespace bkshepherd
#endif
#endif
