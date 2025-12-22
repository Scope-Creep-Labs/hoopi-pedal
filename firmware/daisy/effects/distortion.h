/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef DISTORTION_H
#define DISTORTION_H
#include <string.h>
#include "../hoopi.h"
#include "daisysp.h"
#include "distortion_module.h"
#include "galaxy_lite.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

DistortionModule *distortionModule;
GalaxyLite distortionGalaxyLiteReverb;

// Knobs: 1-3 for Distortion (matching Amp/NAM order), 4-6 for Reverb
Parameter distGain, distLevel, distType;
Parameter distReverbSize, distReverbDecay, distReverbInputGain;

// SDRAM buffers for GalaxyLite reverb (right channel)
static float DSY_SDRAM_BSS distReverbPreDelay[GalaxyLite::MAX_PREDELAY];
static float DSY_SDRAM_BSS distReverbFdn0[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS distReverbFdn1[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS distReverbFdn2[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS distReverbFdn3[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS distReverbDiffuser[GalaxyLite::DIFFUSION_STEPS][GalaxyLite::CHANNELS][GalaxyLite::MAX_DIFFUSER_DELAY];

// This runs at a fixed rate, to prepare audio samples
// Left channel: Distortion
// Right channel: GalaxyLite reverb
static void ProcessDistortion(AudioHandle::InputBuffer  in,
                              AudioHandle::OutputBuffer out,
                              size_t                    size)
{
    // Update Distortion parameters (knobs 1-3: Gain, Level, Type - matching Amp/NAM)
    distortionModule->SetParameterAsFloat(1, distGain.Process());   // Knob 1 -> Gain (param 1)
    distortionModule->SetParameterAsFloat(0, distLevel.Process());  // Knob 2 -> Level (param 0)
    distortionModule->SetParameterAsBinnedValue(3, static_cast<int>(distType.Process() * 5.99f) + 1);  // Knob 3 -> Type

    // Update reverb parameters (knobs 4-6)
    float reverbSize = distReverbSize.Process();
    float reverbDecay = distReverbDecay.Process();
    float inputGainParam = distReverbInputGain.Process();
    float inputGain = 1.0f + inputGainParam * 19.0f;  // 1-20x gain (matches Galaxy)

    distortionGalaxyLiteReverb.SetSize(reverbSize);
    distortionGalaxyLiteReverb.SetDecay(reverbDecay);

    // Apply global GalaxyLite params (shared mic reverb settings)
    distortionGalaxyLiteReverb.SetDamping(galaxyLiteDamping / 255.0f);
    distortionGalaxyLiteReverb.SetPreDelay(galaxyLitePreDelay / 255.0f);
    distortionGalaxyLiteReverb.SetMix(galaxyLiteMix / 255.0f);

    for (size_t i = 0; i < size; i++)
    {
        float inputL = in[0][i];
        float inputR = in[1][i];
        ProcessInputChain(inputL, inputR);  // Process L channel (guitar)

        // Left channel: Distortion processing (mono in from left)
        distortionModule->ProcessMono(inputL);
        float distOut = distortionModule->GetAudioLeft();

        // Right channel: Reverb processing (mono in from right, with input gain)
        float reverbOut = distortionGalaxyLiteReverb.Process(inputR * inputGain);

        // Get backing track channels
        float backingL = in[2][i];  // Guitar backing
        float backingR = in[3][i];  // Mic backing

        // Mix backing track into guitar (left channel)
        float mixedL = ApplyBackingTrackMix(distOut, backingL);
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
                ApplyOutputBlend(distOut, reverbOut, out[2][i], out[3][i]);
            } else {
                out[2][i] = distOut;
                out[3][i] = reverbOut;
            }
        }
    }
}

void InitDistortion(float samplerate) {
    // Distortion controls (knobs 1-3: Gain, Level, Type - matching Amp/NAM order)
    distGain.Init(hw.knob[HoopiPedal::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);   // Knob 1: Gain
    distLevel.Init(hw.knob[HoopiPedal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);  // Knob 2: Level
    distType.Init(hw.knob[HoopiPedal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);   // Knob 3: Type

    // Reverb controls (knobs 4-6)
    distReverbSize.Init(hw.knob[HoopiPedal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    distReverbDecay.Init(hw.knob[HoopiPedal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    distReverbInputGain.Init(hw.knob[HoopiPedal::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    // Initialize Distortion module
    distortionModule = new DistortionModule();
    distortionModule->Init(samplerate);

    // Initialize GalaxyLite reverb with SDRAM buffers
    distortionGalaxyLiteReverb.Init(samplerate,
                                    distReverbPreDelay,
                                    distReverbFdn0, distReverbFdn1, distReverbFdn2, distReverbFdn3,
                                    distReverbDiffuser);
    // GalaxyLite defaults (damping, predelay, mix) are set via effectConfigs and applied in Process
}

#endif /* DISTORTION_H */
