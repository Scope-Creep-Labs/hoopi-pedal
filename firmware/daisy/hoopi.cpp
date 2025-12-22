/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "effects/input_processing.h"
#include "effects/ampsim.h"
#include "effects/chorus.h"
#include "effects/delay.h"
#include "effects/distortion.h"
#include "effects/nam.h"
#include "effects/cloudseed_reverb.h"
#include "effects/tremolo.h"
#include "effects/galaxy.h"


using namespace daisy;
using namespace daisysp;

// Implementation of GetEffectModule - returns effect module by index
bkshepherd::BaseEffectModule* GetEffectModule(uint8_t effectIdx) {
    switch (effectIdx) {
        case Effect::Galaxy:     return galaxyModule;
        case Effect::Reverb:     return cloudSeedModule;
        case Effect::AmpSim:     return ampSimModule;
        case Effect::NamSim:     return namModule;
        case Effect::Distortion: return distortionModule;
        case Effect::Delay:      return delayModule;
        case Effect::Tremolo:    return tremoloModule;
        case Effect::ChorusSim:  return chorusModule;
        default:                 return nullptr;
    }
}

// This runs at a fixed rate, to prepare audio samples
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    if(bypass)
    {
        for (size_t i = 0; i < size; i++)
        {
            float inputL = in[0][i];
            float inputR = in[1][i];
            ProcessInputChain(inputL, inputR);  // Only L is processed

            // Get backing track channels
            float backingL = in[2][i];  // Guitar backing
            float backingR = in[3][i];  // Mic backing

            // Mix backing track into guitar (left channel)
            float mixedL = ApplyBackingTrackMix(inputL, backingL);
            // Mix backing track into mic (right channel) if enabled
            float mixedR = backingTrackBlendMic ? ApplyBackingTrackMix(inputR, backingR) : inputR;

            if (is_recording)
            {
                // Main outputs always get backing track mix
                out[0][i] = mixedL;
                out[1][i] = mixedR;

                // Recording outputs: blend if enabled, otherwise live signal only
                if (backingTrackRecordBlend) {
                    out[2][i] = mixedL;
                    out[3][i] = mixedR;
                } else {
                    out[2][i] = inputL;
                    out[3][i] = inputR;
                }
            }
            else
            {
                out[0][i] = mixedL;
                out[1][i] = mixedR;

                out[2][i] = 0.0f;
                out[3][i] = 0.0f;
            }
        }
    }
    else {
        hw.ProcessAnalogControls();
        switch (enabled_effect) {
            case Effect::Galaxy:
                ProcessGalaxy(in, out, size);
                break;
            case Effect::Reverb:
                ProcessCloudSeed(in, out, size);
                break;
            case Effect::AmpSim:
                ProcessAmpSim(in, out, size);
                break;
            case Effect::ChorusSim:
                ProcessChorus(in, out, size);
                break;
            case Effect::NamSim:
                ProcessNam(in, out, size);
                break;
            case Effect::Distortion:
                ProcessDistortion(in, out, size);
                break;
            case Effect::Delay:
                ProcessDelay(in, out, size);
                break;
            case Effect::Tremolo:
                ProcessTremolo(in, out, size);
                break;
            default:
                ProcessGalaxy(in, out, size);
                break;
        }
    }
}
constexpr unsigned BLOCK_SIZE=48;  // 48 = 1 ms worth of samples @ 48 kHz

#define DEBUG_PRINT 0

int main(void)
{
    hw.Init(true);

#if DEBUG_PRINT
    hw.seed.StartLog(false);
#endif
    // System::Delay(5000);
    // hw.seed.PrintLine("Starting daisy riffpod...");

    hw.SetAudioBlockSize(BLOCK_SIZE); // Up to 256 reduces stuttering when using 2 tap delay (due to processing)

    InitSwitches();

    InitLeds();

    AudioHandle::Config audioConfig;
    audioConfig.blocksize = BLOCK_SIZE;
    ConfigureTwinAudio(audioConfig, true);

    bypass = true;
    hw.StartAdc();

    UpdateButtons();
    UpdateSwitches();

    InitInputProcessor(hw.AudioSampleRate());
    UpdateInputProcessorMode();  // Set initial mode from toggle switch
    InitGalaxy(hw.AudioSampleRate());
    InitCloudSeed(hw.AudioSampleRate());
    InitAmpSim(hw.AudioSampleRate());
    InitChorus(hw.AudioSampleRate());
    InitNam(hw.AudioSampleRate());
    InitDistortion(hw.AudioSampleRate());
    InitDelay(hw.AudioSampleRate());
    InitTremolo(hw.AudioSampleRate());

    InitUart();

    System::Delay(10000);
    SendFWVersion();
    System::Delay(1000);
    SendToggleValues();

    enabled_effect = Effect::Galaxy;
    hw.StartAudio(AudioCallback);

    while(1)
    {
        hw.ProcessDigitalControls();

        CheckFsw1LongPress();

        UpdateButtons();
        UpdateSwitches();



    #if DEBUG_PRINT
        if (changed1)
        {
            hw.seed.PrintLine("sw1 %d", (int)toggleValues[0]);
            // hw.seed.PrintLine("ramp %d triggermode %d", (int)(1000 * ramp), triggerMode ? 1 : 0);
        }
        if (changed2)
        {
            hw.seed.PrintLine("sw2 %d", (int)toggleValues[1]);
        }
        if (changed3)
        {
            hw.seed.PrintLine("sw3 %d", (int)toggleValues[2]);
            hw.seed.PrintLine("effect %d", (int)enabled_effect);
        }

        // Print knob values when they change (scaled to 0-100)
        static float prevKnobs[6] = {-1, -1, -1, -1, -1, -1};
        hw.ProcessAnalogControls();  // Update ADC readings
        for (int k = 0; k < 6; k++)
        {
            float val = hw.GetKnobValue(static_cast<HoopiPedal::Knob>(k));
            if (knobMoved(prevKnobs[k], val))
            {
                hw.seed.PrintLine("knob%d %d", k + 1, (int)(val * 100));
                prevKnobs[k] = val;
            }
        }
    #endif

        // Poll for UART frames (v2 protocol with START byte and checksum)
        if (UartPollReceive())
        {
            // rx_payload[0] = CMD, rx_payload[1..n] = DATA
            uint8_t cmd = rx_payload[0];

            if (cmd == 0x02)  // Start Recording
            {
                StartRecording();
            }
            else if (cmd == 0x01)  // Stop Recording
            {
                StopRecording();
            }
            else if (cmd == 0x03)  // Reset to Bootloader
            {
                UartSendCmd(0x05);  // ACK Bootloader
                System::Delay(1000);
                System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
            }
            else if (cmd == 0x08)  // Set Parameter
            {
                // DATA: effect_idx, param_id, value, [extra]
                uint8_t effectIdx = rx_payload[1];
                uint8_t paramId = rx_payload[2];
                uint8_t value = rx_payload[3];

                bool handled = false;

                // Global params (param_id 0-3, effect index ignored)
                if (paramId == 0) {
                    // Blend mode: byte3=mode, byte4=applyToRecording
                    outputBlendMode = value;
                    applyBlendToRecording = (rx_payload_len > 4) ? rx_payload[4] : 0;
                    handled = true;
                }
                else if (paramId == 1) {
                    galaxyLiteDamping = value;
                    handled = true;
                }
                else if (paramId == 2) {
                    galaxyLitePreDelay = value;
                    handled = true;
                }
                else if (paramId == 3) {
                    galaxyLiteMix = value;
                    handled = true;
                }
                // Compressor params (param_id 4-8)
                else if (paramId == 4) {
                    compThreshold = value;
                    inputProcessor.SetCompThreshold(value);
                    handled = true;
                }
                else if (paramId == 5) {
                    compRatio = value;
                    inputProcessor.SetCompRatio(value);
                    handled = true;
                }
                else if (paramId == 6) {
                    compAttack = value;
                    inputProcessor.SetCompAttack(value);
                    handled = true;
                }
                else if (paramId == 7) {
                    compRelease = value;
                    inputProcessor.SetCompRelease(value);
                    handled = true;
                }
                else if (paramId == 8) {
                    compMakeupGain = value;
                    inputProcessor.SetCompMakeupGain(value);
                    handled = true;
                }
                // Noise gate params (param_id 9-12)
                else if (paramId == 9) {
                    gateThreshold = value;
                    inputProcessor.SetGateThreshold(value);
                    handled = true;
                }
                else if (paramId == 10) {
                    gateAttack = value;
                    inputProcessor.SetGateAttack(value);
                    handled = true;
                }
                else if (paramId == 11) {
                    gateHold = value;
                    inputProcessor.SetGateHold(value);
                    handled = true;
                }
                else if (paramId == 12) {
                    gateRelease = value;
                    inputProcessor.SetGateRelease(value);
                    handled = true;
                }
                // EQ params (param_id 30-36)
                else if (paramId == 30) {
                    eqEnabled = value;
                    inputProcessor.SetEqEnabled(value != 0);
                    handled = true;
                }
                else if (paramId == 31) {
                    eqLowGain = value;
                    inputProcessor.SetEqLowGain(value);
                    handled = true;
                }
                else if (paramId == 32) {
                    eqMidGain = value;
                    inputProcessor.SetEqMidGain(value);
                    handled = true;
                }
                else if (paramId == 33) {
                    eqHighGain = value;
                    inputProcessor.SetEqHighGain(value);
                    handled = true;
                }
                else if (paramId == 34) {
                    eqLowFreq = value;
                    inputProcessor.SetEqLowFreq(value);
                    handled = true;
                }
                else if (paramId == 35) {
                    eqMidFreq = value;
                    inputProcessor.SetEqMidFreq(value);
                    handled = true;
                }
                else if (paramId == 36) {
                    eqHighFreq = value;
                    inputProcessor.SetEqHighFreq(value);
                    handled = true;
                }
                // Effect-specific params via MIDI CC (param_id >= 14, excluding 30-36 used by EQ)
                else if (paramId >= 14 && paramId <= 29) {
                    bkshepherd::BaseEffectModule* module = GetEffectModule(effectIdx);
                    if (module != nullptr) {
                        // Use MidiCCValueNotification to set parameter via MIDI CC mapping
                        // Value is 0-255 from UART, scale to 0-127 for MIDI CC
                        uint8_t midiValue = value >> 1;  // 0-255 -> 0-127
                        module->MidiCCValueNotification(paramId, midiValue);
                        handled = true;
                    }
                }
                else if (paramId >= 37) {
                    bkshepherd::BaseEffectModule* module = GetEffectModule(effectIdx);
                    if (module != nullptr) {
                        uint8_t midiValue = value >> 1;  // 0-255 -> 0-127
                        module->MidiCCValueNotification(paramId, midiValue);
                        handled = true;
                    }
                }

                if (handled) {
                    // ACK with confirmed values
                    uint8_t ack_data[] = {effectIdx, paramId, value};
                    UartSendFrame(0x08, ack_data, 3);  // Param ACK
                }
            }
            else if (cmd == 0x09)  // Request Knob Values
            {
                SendKnobValues();
            }
            else if (cmd == 0x0A)  // Arm Recording
            {
                // ArmRecording() checks if already recording and sends ACK
                ArmRecording();
            }
            else if (cmd == 0x0B)  // Disarm Recording
            {
                // Cancel armed state, no response per spec
                DisarmRecording();
            }
            else if (cmd == 0x0C)  // Backing Track
            {
                // DATA 0: record_blend (0=don't blend into recording, 1=blend)
                // DATA 1: blend ratio (0-127: 0=live only, 127=equal mix)
                // DATA 2: blend_mic (0=guitar only, 1=also blend mic channel)
                backingTrackRecordBlend = (rx_payload[1] != 0);
                backingTrackBlendRatio = rx_payload[2];
                backingTrackBlendMic = (rx_payload[3] != 0);
                // No response per spec
            }
            else if (cmd == 0xFF)  // Select Effect
            {
                // DATA: effect_id
                EnableEffect(rx_payload[1]);
            }
            else if (cmd == 0x10)  // Control Change (Legacy MIDI CC)
            {
                // DATA: cc_num, cc_val
                uint8_t ccNum = rx_payload[1];
                uint8_t ccVal = rx_payload[2];
                bkshepherd::BaseEffectModule* module = GetEffectModule(static_cast<uint8_t>(enabled_effect));
                if (module != nullptr) {
                    module->MidiCCValueNotification(ccNum, ccVal);
                }
            }
            // Ignore unrecognized commands

            ackUartCommand();  // Quick LED blink after command processed
        }

        if (changed2) {
            UpdateInputProcessorMode();
        }

        if (changed1 || changed2 || changed3) {
            SendToggleValues();
        }

        // Update LED blinking if recording is armed
        UpdateArmBlinking();

        System::Delay(50);
    }
}
