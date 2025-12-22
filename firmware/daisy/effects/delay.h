/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef DELAY_WRAPPER_H
#define DELAY_WRAPPER_H

#include <string.h>
#include "../hoopi.h"
#include "daisysp.h"
#include "delay_module.h"
#include "galaxy_lite.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

DelayModule *delayModule;
GalaxyLite delayGalaxyLiteReverb;

// Knobs: 1-4 for Delay, 5-6 for Reverb
Parameter delayTime, delayFeedback, delayMix, delayType;
Parameter delayReverbSize, delayReverbDecay;

// SDRAM buffers for GalaxyLite reverb (right channel)
static float DSY_SDRAM_BSS delayReverbPreDelay[GalaxyLite::MAX_PREDELAY];
static float DSY_SDRAM_BSS delayReverbFdn0[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS delayReverbFdn1[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS delayReverbFdn2[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS delayReverbFdn3[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS delayReverbDiffuser[GalaxyLite::DIFFUSION_STEPS][GalaxyLite::CHANNELS][GalaxyLite::MAX_DIFFUSER_DELAY];

// Process audio samples
// Left channel: Delay (guitar)
// Right channel: GalaxyLite reverb (mic)
static void ProcessDelay(AudioHandle::InputBuffer  in,
                         AudioHandle::OutputBuffer out,
                         size_t                    size)
{
    // Update Delay parameters (knobs 1-4)
    delayModule->SetParameterAsFloat(0, delayTime.Process());      // Time
    delayModule->SetParameterAsFloat(1, delayFeedback.Process());  // Feedback
    delayModule->SetParameterAsFloat(2, delayMix.Process());       // Mix
    delayModule->SetParameterAsBinnedValue(3, static_cast<int>(delayType.Process() * 3.99f) + 1);  // Type

    // Update reverb parameters (knobs 5-6)
    float reverbSize = delayReverbSize.Process();
    float reverbDecay = delayReverbDecay.Process();

    delayGalaxyLiteReverb.SetSize(reverbSize);
    delayGalaxyLiteReverb.SetDecay(reverbDecay);

    // Apply global GalaxyLite params (shared mic reverb settings)
    delayGalaxyLiteReverb.SetDamping(galaxyLiteDamping / 255.0f);
    delayGalaxyLiteReverb.SetPreDelay(galaxyLitePreDelay / 255.0f);
    delayGalaxyLiteReverb.SetMix(galaxyLiteMix / 255.0f);

    for (size_t i = 0; i < size; i++)
    {
        float inputL = in[0][i];
        float inputR = in[1][i];
        ProcessInputChain(inputL, inputR);  // Process L channel (guitar)

        // Left channel: Delay processing
        delayModule->ProcessMono(inputL);
        float delayOut = delayModule->GetAudioLeft();

        // Right channel: Reverb processing (mic input)
        float reverbOut = delayGalaxyLiteReverb.Process(inputR);

        // Get backing track channels
        float backingL = in[2][i];  // Guitar backing
        float backingR = in[3][i];  // Mic backing

        // Mix backing track into guitar (left channel)
        float mixedL = ApplyBackingTrackMix(delayOut, backingL);
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
                ApplyOutputBlend(delayOut, reverbOut, out[2][i], out[3][i]);
            } else {
                out[2][i] = delayOut;
                out[3][i] = reverbOut;
            }
        }
    }
}

void InitDelay(float samplerate) {
    // Delay controls (knobs 1-4)
    delayTime.Init(hw.knob[HoopiPedal::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    delayFeedback.Init(hw.knob[HoopiPedal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    delayMix.Init(hw.knob[HoopiPedal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    delayType.Init(hw.knob[HoopiPedal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);

    // Reverb controls (knobs 5-6)
    delayReverbSize.Init(hw.knob[HoopiPedal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    delayReverbDecay.Init(hw.knob[HoopiPedal::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    // Initialize Delay module
    delayModule = new DelayModule();
    delayModule->Init(samplerate);

    // Initialize GalaxyLite reverb with SDRAM buffers
    delayGalaxyLiteReverb.Init(samplerate,
                               delayReverbPreDelay,
                               delayReverbFdn0, delayReverbFdn1, delayReverbFdn2, delayReverbFdn3,
                               delayReverbDiffuser);
    // GalaxyLite defaults (damping, predelay, mix) are set via effectConfigs and applied in Process
}

#endif /* DELAY_WRAPPER_H */
