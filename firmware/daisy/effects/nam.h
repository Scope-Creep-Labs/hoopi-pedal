/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef NAM_H
#define NAM_H
#include <string.h>
#include "../hoopi.h"
#include "../nam_models.h"
#include "daisysp.h"
#include "nam_module.h"
#include "galaxy_lite.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

NamModule *namModule;
GalaxyLite galaxyLiteReverb;

// Knobs: 1-3 for NAM, 4-6 for Reverb
Parameter namGain, namLevel, namModel;
Parameter namReverbSize, namReverbDecay, namReverbInputGain;

// SDRAM buffers for GalaxyLite reverb (right channel)
static float DSY_SDRAM_BSS namReverbPreDelay[GalaxyLite::MAX_PREDELAY];
static float DSY_SDRAM_BSS namReverbFdn0[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS namReverbFdn1[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS namReverbFdn2[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS namReverbFdn3[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS namReverbDiffuser[GalaxyLite::DIFFUSION_STEPS][GalaxyLite::CHANNELS][GalaxyLite::MAX_DIFFUSER_DELAY];

// Buffers for block processing
static float namInputBuf[NAM_BLOCK];
static float namOutputBuf[NAM_BLOCK];

// This runs at a fixed rate, to prepare audio samples
// Left channel: NAM (neural amp model) - block processing
// Right channel: GalaxyLite reverb - sample by sample
static void ProcessNam(AudioHandle::InputBuffer  in,
                       AudioHandle::OutputBuffer out,
                       size_t                    size)
{
    // Update NAM parameters (knobs 1-3)
    namModule->SetParameterAsFloat(0, namGain.Process());
    namModule->SetParameterAsFloat(1, namLevel.Process());
    // Scale knob to current model count (1-8 models)
    int modelCount = GetModelCount();
    int modelIndex = static_cast<int>(namModel.Process() * (modelCount - 0.01f)) + 1;
    namModule->SetParameterAsBinnedValue(2, modelIndex);

    // Update reverb parameters (knobs 4-6)
    float reverbSize = namReverbSize.Process();
    float reverbDecay = namReverbDecay.Process();
    float inputGainParam = namReverbInputGain.Process();
    float inputGain = 1.0f + inputGainParam * 19.0f;  // 1-20x gain (matches Galaxy)

    galaxyLiteReverb.SetSize(reverbSize);
    galaxyLiteReverb.SetDecay(reverbDecay);

    // Apply global GalaxyLite params (shared mic reverb settings)
    galaxyLiteReverb.SetDamping(galaxyLiteDamping / 255.0f);
    galaxyLiteReverb.SetPreDelay(galaxyLitePreDelay / 255.0f);
    galaxyLiteReverb.SetMix(galaxyLiteMix / 255.0f);

    // Collect input samples for block processing
    for (size_t i = 0; i < size; i++) {
        float inputL = in[0][i];
        float inputR = in[1][i];
        ProcessInputChain(inputL, inputR);
        namInputBuf[i] = inputL;
    }

    // Process NAM as a block (256 samples at once)
    namModule->ProcessBlock(namInputBuf, namOutputBuf, size);

    // Apply reverb and mix outputs
    for (size_t i = 0; i < size; i++)
    {
        float inputR = in[1][i];
        ProcessInputChain(inputR, inputR);  // Process right channel for reverb

        // Left channel: NAM output
        float namOut = namOutputBuf[i];

        // Right channel: Reverb processing (mono in from right, with input gain)
        float reverbOut = galaxyLiteReverb.Process(inputR * inputGain);

        // Get backing track channels
        float backingL = in[2][i];  // Guitar backing
        float backingR = in[3][i];  // Mic backing

        // Mix backing track into guitar (left channel)
        float mixedL = ApplyBackingTrackMix(namOut, backingL);
        // Mix backing track into mic (right channel) if enabled
        float mixedR = backingTrackBlendMic ? ApplyBackingTrackMix(reverbOut, backingR) : reverbOut;

        // Apply output blend to main outputs
        ApplyOutputBlend(mixedL, mixedR, out[0][i], out[1][i]);

        // Recording outputs: stereo or blended based on setting
        if (backingTrackRecordBlend) {
            // Include backing track in recording
            if (applyBlendToRecording) {
                out[2][i] = out[0][i];
                out[3][i] = out[1][i];
            } else {
                out[2][i] = mixedL;
                out[3][i] = mixedR;
            }
        } else {
            // Recording without backing track
            if (applyBlendToRecording) {
                ApplyOutputBlend(namOut, reverbOut, out[2][i], out[3][i]);
            } else {
                out[2][i] = namOut;
                out[3][i] = reverbOut;
            }
        }
    }
}

void InitNam(float samplerate) {
    // NAM controls (knobs 1-3)
    namGain.Init(hw.knob[HoopiPedal::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    namLevel.Init(hw.knob[HoopiPedal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    namModel.Init(hw.knob[HoopiPedal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);

    // Reverb controls (knobs 4-6)
    namReverbSize.Init(hw.knob[HoopiPedal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    namReverbDecay.Init(hw.knob[HoopiPedal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    namReverbInputGain.Init(hw.knob[HoopiPedal::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    // Initialize NAM module
    namModule = new NamModule();
    namModule->Init(samplerate);

    // Initialize GalaxyLite reverb with SDRAM buffers
    galaxyLiteReverb.Init(samplerate,
                          namReverbPreDelay,
                          namReverbFdn0, namReverbFdn1, namReverbFdn2, namReverbFdn3,
                          namReverbDiffuser);
    // GalaxyLite defaults (damping, predelay, mix) are set via effectConfigs and applied in Process
}

#endif /* NAM_H */
