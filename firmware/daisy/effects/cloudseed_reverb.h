/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef CLOUDSEED_REVERB_H
#define CLOUDSEED_REVERB_H
#include <string.h>
#include "../hoopi.h"
#include "daisysp.h"
#include "cloudseed_module.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

CloudSeedModule *cloudSeedModule;
// Knob layout: PreDelay, Decay, In Gain L, Tone, Mix, In Gain R
Parameter csPreDelay, csDecay, csInGainL, csTone, csMix, csInGainR;

static void ProcessCloudSeed(AudioHandle::InputBuffer in,
                             AudioHandle::OutputBuffer out,
                             size_t size)
{
    // Param 0: PreDelay (knob 1)
    cloudSeedModule->SetParameterAsFloat(0, csPreDelay.Process());
    // Param 2: Decay (knob 2)
    cloudSeedModule->SetParameterAsFloat(2, csDecay.Process());
    // Param 10: In Gain L (knob 3)
    cloudSeedModule->SetParameterAsFloat(10, csInGainL.Process());
    // Param 5: Tone (knob 4)
    cloudSeedModule->SetParameterAsFloat(5, csTone.Process());
    // Param 1: Mix (knob 5)
    cloudSeedModule->SetParameterAsFloat(1, csMix.Process());
    // Param 11: In Gain R (knob 6)
    cloudSeedModule->SetParameterAsFloat(11, csInGainR.Process());

    for (size_t i = 0; i < size; i++)
    {
        float inputL = in[0][i];
        float inputR = in[1][i];
        ProcessInputChain(inputL, inputR);

        cloudSeedModule->ProcessStereo(inputL, inputR);
        float outputL = cloudSeedModule->GetAudioLeft();
        float outputR = cloudSeedModule->GetAudioRight();

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

void InitCloudSeed(float samplerate) {
    // Knob layout: PreDelay, Decay, In Gain L, Tone, Mix, In Gain R
    csPreDelay.Init(hw.knob[HoopiPedal::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    csDecay.Init(hw.knob[HoopiPedal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    csInGainL.Init(hw.knob[HoopiPedal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    csTone.Init(hw.knob[HoopiPedal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    csMix.Init(hw.knob[HoopiPedal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    csInGainR.Init(hw.knob[HoopiPedal::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    cloudSeedModule = new CloudSeedModule();
    cloudSeedModule->Init(samplerate);
}

#endif /* CLOUDSEED_REVERB_H */
