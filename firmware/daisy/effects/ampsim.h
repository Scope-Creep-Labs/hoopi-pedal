/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef AMPSIM_H
#define AMPSIM_H
#include <string.h>
#include "../hoopi.h"
#include "daisysp.h"
#include "amp_module.h"
#include "galaxy_lite.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

AmpModule *ampSimModule;
GalaxyLite galaxyLiteReverbAmp;

// Knobs: 1-3 for AmpSim, 4-6 for Reverb
Parameter ampSimGain, ampSimLevel, ampSimModel;
Parameter ampSimReverbSize, ampSimReverbDecay, ampSimReverbInputGain;

// SDRAM buffers for GalaxyLite reverb (right channel)
static float DSY_SDRAM_BSS ampSimReverbPreDelay[GalaxyLite::MAX_PREDELAY];
static float DSY_SDRAM_BSS ampSimReverbFdn0[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS ampSimReverbFdn1[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS ampSimReverbFdn2[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS ampSimReverbFdn3[GalaxyLite::MAX_FDN_DELAY];
static float DSY_SDRAM_BSS ampSimReverbDiffuser[GalaxyLite::DIFFUSION_STEPS][GalaxyLite::CHANNELS][GalaxyLite::MAX_DIFFUSER_DELAY];

// This runs at a fixed rate, to prepare audio samples
// Left channel: AmpSim (neural amp model)
// Right channel: GalaxyLite reverb
static void ProcessAmpSim(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    // Update AmpSim parameters (knobs 1-3)
    ampSimModule->SetParameterAsFloat(0, ampSimGain.Process());     // Gain
    ampSimModule->SetParameterAsFloat(2, ampSimLevel.Process());    // Level
    ampSimModule->SetParameterAsBinnedValue(4, static_cast<int>(ampSimModel.Process() * 6.99f) + 1); // Model

    // Update reverb parameters (knobs 4-6)
    float reverbSize = ampSimReverbSize.Process();
    float reverbDecay = ampSimReverbDecay.Process();
    float inputGainParam = ampSimReverbInputGain.Process();
    float inputGain = 1.0f + inputGainParam * 19.0f;  // 1-20x gain (matches NAM combo)

    galaxyLiteReverbAmp.SetSize(reverbSize);
    galaxyLiteReverbAmp.SetDecay(reverbDecay);

    // Apply global GalaxyLite params (shared mic reverb settings)
    galaxyLiteReverbAmp.SetDamping(galaxyLiteDamping / 255.0f);
    galaxyLiteReverbAmp.SetPreDelay(galaxyLitePreDelay / 255.0f);
    galaxyLiteReverbAmp.SetMix(galaxyLiteMix / 255.0f);

    for (size_t i = 0; i < size; i++)
    {
        float inputL = in[0][i];
        float inputR = in[1][i];
        ProcessInputChain(inputL, inputR);  // Process L channel (guitar)

        // Left channel: AmpSim processing (mono in from left)
        ampSimModule->ProcessMono(inputL);
        float ampOut = ampSimModule->GetAudioLeft();

        // Right channel: Reverb processing (mono in from right, with input gain)
        float reverbOut = galaxyLiteReverbAmp.Process(inputR * inputGain);

        // Get backing track channels
        float backingL = in[2][i];  // Guitar backing
        float backingR = in[3][i];  // Mic backing

        // Mix backing track into guitar (left channel)
        float mixedL = ApplyBackingTrackMix(ampOut, backingL);
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
                ApplyOutputBlend(ampOut, reverbOut, out[2][i], out[3][i]);
            } else {
                out[2][i] = ampOut;
                out[3][i] = reverbOut;
            }
        }
    }
}

void InitAmpSim(float samplerate) {
    // AmpSim controls (knobs 1-3)
    ampSimGain.Init(hw.knob[HoopiPedal::KNOB_1], 0.0f, 1.0f, Parameter::LOGARITHMIC);
    ampSimLevel.Init(hw.knob[HoopiPedal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    ampSimModel.Init(hw.knob[HoopiPedal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);

    // Reverb controls (knobs 4-6)
    ampSimReverbSize.Init(hw.knob[HoopiPedal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    ampSimReverbDecay.Init(hw.knob[HoopiPedal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    ampSimReverbInputGain.Init(hw.knob[HoopiPedal::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    // Initialize AmpSim module
    ampSimModule = new AmpModule();
    ampSimModule->Init(samplerate);

    // Set defaults for AmpSim:
    // Mix = full wet (param 1)
    ampSimModule->SetParameterAsFloat(1, 1.0f);
    // Tone = 20kHz / no filter (param 3)
    ampSimModule->SetParameterAsFloat(3, 1.0f);
    // IR = Marsh / first one (param 5)
    ampSimModule->SetParameterAsBinnedValue(5, 1);  // Binned values are 1-indexed
    // Enable neural model and IR
    ampSimModule->SetParameterAsBool(6, true);      // NeuralModel on
    ampSimModule->SetParameterAsBool(7, true);      // IR on

    // Initialize GalaxyLite reverb with SDRAM buffers
    galaxyLiteReverbAmp.Init(samplerate,
                             ampSimReverbPreDelay,
                             ampSimReverbFdn0, ampSimReverbFdn1, ampSimReverbFdn2, ampSimReverbFdn3,
                             ampSimReverbDiffuser);
    // GalaxyLite defaults (damping, predelay, mix) are set via effectConfigs and applied in Process
}

#endif /* AMPSIM_H */
