/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef GALAXY_REVERB_WRAPPER_H
#define GALAXY_REVERB_WRAPPER_H

#include <string.h>
#include "../hoopi.h"
#include "galaxy_reverb_module.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

GalaxyReverbModule *galaxyModule;
Parameter galaxySize, galaxyDecay, galaxyGainL, galaxyDamping, galaxyMix, galaxyGainR;

// Process audio samples through the Galaxy reverb
static void ProcessGalaxy(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    // Update parameters from knobs
    // Param 0: Size, Param 1: Decay, Param 2: Input Gain L
    // Param 3: Damping, Param 4: Mix, Param 5: Input Gain R
    galaxyModule->SetParameterAsFloat(0, galaxySize.Process());
    galaxyModule->SetParameterAsFloat(1, galaxyDecay.Process());
    galaxyModule->SetParameterAsFloat(2, galaxyGainL.Process());
    galaxyModule->SetParameterAsFloat(3, galaxyDamping.Process());
    galaxyModule->SetParameterAsFloat(4, galaxyMix.Process());
    galaxyModule->SetParameterAsFloat(5, galaxyGainR.Process());

    for (size_t i = 0; i < size; i++)
    {
        float inputL = in[0][i];
        float inputR = in[1][i];
        ProcessInputChain(inputL, inputR);  // Process L channel (guitar)

        galaxyModule->ProcessStereo(inputL, inputR);

        float outputL = galaxyModule->GetAudioLeft();
        float outputR = galaxyModule->GetAudioRight();

        // Get backing track channels
        float backingL = in[2][i];  // Guitar backing
        float backingR = in[3][i];  // Mic backing

        // Mix backing track into guitar (left channel)
        float mixedL = ApplyBackingTrackMix(outputL, backingL);
        // Mix backing track into mic (right channel) if enabled
        float mixedR = backingTrackBlendMic ? ApplyBackingTrackMix(outputR, backingR) : outputR;

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
                ApplyOutputBlend(outputL, outputR, out[2][i], out[3][i]);
            } else {
                out[2][i] = outputL;
                out[3][i] = outputR;
            }
        }
    }
}

void InitGalaxy(float samplerate) {
    // Knob 1: Size (room size 20-300ms)
    galaxySize.Init(hw.knob[HoopiPedal::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);

    // Knob 2: Decay (RT60 0.5-20s)
    galaxyDecay.Init(hw.knob[HoopiPedal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);

    // Knob 3: Input Gain L (0-2x)
    galaxyGainL.Init(hw.knob[HoopiPedal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);

    // Knob 4: Damping (HF rolloff)
    galaxyDamping.Init(hw.knob[HoopiPedal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);

    // Knob 5: Mix (dry/wet)
    galaxyMix.Init(hw.knob[HoopiPedal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);

    // Knob 6: Input Gain R (0-2x)
    galaxyGainR.Init(hw.knob[HoopiPedal::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    galaxyModule = new GalaxyReverbModule();
    galaxyModule->Init(samplerate);
}

#endif /* GALAXY_REVERB_WRAPPER_H */
