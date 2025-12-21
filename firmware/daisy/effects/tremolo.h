/**
 * Hoopi Pedal - Effects module
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef TREMOLO_WRAPPER_H
#define TREMOLO_WRAPPER_H

#include <string.h>
#include "../hoopi.h"
#include "daisysp.h"
#include "tremolo_module.h"
#include "galaxy_lite.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

TremoloModule *tremoloModule;
GalaxyLite tremoloGalaxyLiteReverb;

// Knobs: 1-4 for Tremolo, 5-6 for Reverb
Parameter tremoloRate, tremoloDepth, tremoloWave, tremoloModRate;
Parameter tremoloReverbSize, tremoloReverbDecay;

// SDRAM buffers for GalaxyLite reverb (right channel)
static float DSY_SDRAM_BSS tremoloReverbPreDelay[GalaxyLite::MAX_PREDELAY];
static float DSY_SDRAM_BSS tremoloReverbFdn0[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS tremoloReverbFdn1[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS tremoloReverbFdn2[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS tremoloReverbFdn3[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS tremoloReverbDiffuser[GalaxyLite::DIFFUSION_STEPS][GalaxyLite::CHANNELS][GalaxyLite::MAX_DIFFUSER_DELAY];

// Process audio samples
// Left channel: Tremolo (guitar)
// Right channel: GalaxyLite reverb (mic)
static void ProcessTremolo(AudioHandle::InputBuffer  in,
                           AudioHandle::OutputBuffer out,
                           size_t                    size)
{
    // Update Tremolo parameters (knobs 1-4)
    tremoloModule->SetParameterAsFloat(0, tremoloRate.Process());      // Rate
    tremoloModule->SetParameterAsFloat(1, tremoloDepth.Process());     // Depth
    tremoloModule->SetParameterAsBinnedValue(2, static_cast<int>(tremoloWave.Process() * 4.99f) + 1);  // Wave
    tremoloModule->SetParameterAsFloat(3, tremoloModRate.Process());   // Mod Rate

    // Update reverb parameters (knobs 5-6)
    float reverbSize = tremoloReverbSize.Process();
    float reverbDecay = tremoloReverbDecay.Process();

    tremoloGalaxyLiteReverb.SetSize(reverbSize);
    tremoloGalaxyLiteReverb.SetDecay(reverbDecay);

    // Apply global GalaxyLite params (shared mic reverb settings)
    tremoloGalaxyLiteReverb.SetDamping(galaxyLiteDamping / 255.0f);
    tremoloGalaxyLiteReverb.SetPreDelay(galaxyLitePreDelay / 255.0f);
    tremoloGalaxyLiteReverb.SetMix(galaxyLiteMix / 255.0f);

    for (size_t i = 0; i < size; i++)
    {
        float inputL = in[0][i];
        float inputR = in[1][i];
        ProcessInputChain(inputL, inputR);  // Process L channel (guitar)

        // Left channel: Tremolo processing
        tremoloModule->ProcessMono(inputL);
        float tremoloOut = tremoloModule->GetAudioLeft();

        // Right channel: Reverb processing (mic input)
        float reverbOut = tremoloGalaxyLiteReverb.Process(inputR);

        // Apply output blend to main outputs
        ApplyOutputBlend(tremoloOut, reverbOut, out[0][i], out[1][i]);

        // Recording outputs: stereo or blended based on setting
        if (applyBlendToRecording) {
            out[2][i] = out[0][i];
            out[3][i] = out[1][i];
        } else {
            out[2][i] = tremoloOut;
            out[3][i] = reverbOut;
        }
    }
}

void InitTremolo(float samplerate) {
    // Tremolo controls (knobs 1-4)
    tremoloRate.Init(hw.knob[HoopiPedal::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    tremoloDepth.Init(hw.knob[HoopiPedal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    tremoloWave.Init(hw.knob[HoopiPedal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    tremoloModRate.Init(hw.knob[HoopiPedal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);

    // Reverb controls (knobs 5-6)
    tremoloReverbSize.Init(hw.knob[HoopiPedal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    tremoloReverbDecay.Init(hw.knob[HoopiPedal::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    // Initialize Tremolo module
    tremoloModule = new TremoloModule();
    tremoloModule->Init(samplerate);

    // Initialize GalaxyLite reverb with SDRAM buffers
    tremoloGalaxyLiteReverb.Init(samplerate,
                                 tremoloReverbPreDelay,
                                 tremoloReverbFdn0, tremoloReverbFdn1, tremoloReverbFdn2, tremoloReverbFdn3,
                                 tremoloReverbDiffuser);
    // GalaxyLite defaults (damping, predelay, mix) are set via effectConfigs and applied in Process
}

#endif /* TREMOLO_WRAPPER_H */
