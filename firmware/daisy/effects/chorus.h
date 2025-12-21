/**
 * Hoopi Pedal - Effects module
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef E295CE91_82B6_45D0_916D_23870E808497
#define E295CE91_82B6_45D0_916D_23870E808497
#include <string.h>
#include "../hoopi.h"
#include "daisysp.h"
#include "chorus_module.h"


using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

ChorusModule *chorusModule;
Parameter chorusWet, chorusDelay, chorusLfoFreq, chorusLfoDepth, chorusFeedback;


// This runs at a fixed rate, to prepare audio samples
static void ProcessChorus(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
  chorusModule->SetParameterAsFloat(0, chorusWet.Process());
  chorusModule->SetParameterAsFloat(1, chorusDelay.Process());
  chorusModule->SetParameterAsFloat(2, chorusLfoFreq.Process());
  chorusModule->SetParameterAsFloat(3, chorusLfoDepth.Process());
  chorusModule->SetParameterAsFloat(4, chorusFeedback.Process());

  for (size_t i = 0; i < size; i++)
  {
    float inputL = in[0][i];
    float inputR = in[1][i];
    ProcessInputChain(inputL, inputR);  // Process L channel (guitar)

    chorusModule->ProcessStereo(inputL, inputR);
    float outputL = chorusModule->GetAudioLeft();
    float outputR = chorusModule->GetAudioRight();

    // Apply output blend to main outputs
    ApplyOutputBlend(outputL, outputR, out[0][i], out[1][i]);

    // Recording outputs: stereo or blended based on setting
    if (applyBlendToRecording) {
        out[2][i] = out[0][i];
        out[3][i] = out[1][i];
    } else {
        out[2][i] = outputL;
        out[3][i] = outputR;
    }
  }
}

void InitChorus(float samplerate) {
    chorusWet.Init(hw.knob[HoopiPedal::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    chorusDelay.Init(hw.knob[HoopiPedal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    chorusLfoFreq.Init(hw.knob[HoopiPedal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    chorusLfoDepth.Init(hw.knob[HoopiPedal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    chorusFeedback.Init(hw.knob[HoopiPedal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);

    chorusModule = new ChorusModule();
    chorusModule->Init(samplerate);
}




#endif /* E295CE91_82B6_45D0_916D_23870E808497 */
