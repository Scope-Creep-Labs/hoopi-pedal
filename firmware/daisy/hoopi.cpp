/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "hoopi.h"
#include "effects/input_processing.h"
#include "effects/ampsim.h"
#include "effects/chorus.h"
#include "effects/delay.h"
#include "effects/distortion.h"
#include "effects/nam.h"
#include "effects/cloudseed_reverb.h"
#include "effects/tremolo.h"
#include "effects/galaxy.h"
#include "effects/looper.h"
#include "effects/nam_wavenet.h"

using namespace daisy;
using namespace daisysp;

// Hardware instance
HoopiPedal hw;

// Audio block size (must match NAM_BLOCK from nam_wavenet.h)
constexpr unsigned BLOCK_SIZE = NAM_BLOCK;

#define DEBUG_PRINT 0

// Audio callback - processes audio samples
void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
{
    if (bypass) {
        // True bypass - straight passthrough with no processing
        for (size_t i = 0; i < size; i++) {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];

            if (is_recording) {
                out[2][i] = in[0][i];
                out[3][i] = in[1][i];
            } else {
                out[2][i] = 0.0f;
                out[3][i] = 0.0f;
            }
        }
    } else {
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

int main(void)
{
    hw.Init(true);

#if DEBUG_PRINT
    hw.seed.StartLog(true);
    System::Delay(1000);
    hw.seed.PrintLine("=== Hoopi Pedal Starting ===");
#endif

    hw.SetAudioBlockSize(BLOCK_SIZE);

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
    UpdateInputProcessorMode();
    InitGalaxy(hw.AudioSampleRate());
    InitCloudSeed(hw.AudioSampleRate());
    InitAmpSim(hw.AudioSampleRate());
    InitChorus(hw.AudioSampleRate());
    InitNam(hw.AudioSampleRate());
    InitDistortion(hw.AudioSampleRate());
    InitDelay(hw.AudioSampleRate());
    InitTremolo(hw.AudioSampleRate());

    InitUart();

    System::Delay(1000);
    SendDeviceInfo();
    System::Delay(1000);
    SendToggleValues();

    enabled_effect = Effect::Galaxy;
    hw.StartAudio(AudioCallback);

    hw.seed.SetLed(true);

    while (1) {
        hw.ProcessDigitalControls();

        CheckFsw1LongPress();
        UpdateButtons();
        UpdateSwitches();
        updateLedBlink();

#if DEBUG_PRINT
        if (changed1) {
            hw.seed.PrintLine("sw1 %d", (int)toggleValues[0]);
        }
        if (changed2) {
            hw.seed.PrintLine("sw2 %d", (int)toggleValues[1]);
        }
        if (changed3) {
            hw.seed.PrintLine("sw3 %d", (int)toggleValues[2]);
            hw.seed.PrintLine("effect %d", (int)enabled_effect);
        }

        // Print knob values when they change
        static float prevKnobs[6] = {-1, -1, -1, -1, -1, -1};
        hw.ProcessAnalogControls();
        for (int k = 0; k < 6; k++) {
            float val = hw.GetKnobValue(static_cast<HoopiPedal::Knob>(k));
            if (knobMoved(prevKnobs[k], val)) {
                hw.seed.PrintLine("knob%d %d", k + 1, (int)(val * 100));
                prevKnobs[k] = val;
            }
        }
#endif

        // Poll for UART frames
        if (UartPollReceive()) {
            uint8_t cmd = rx_payload[0];
            ProcessUartCommand(cmd, rx_payload, rx_payload_len);
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

// Configure twin (4-channel) audio I/O
void ConfigureTwinAudio(AudioHandle::Config audioConfig, bool sai2ClockMaster)
{
    // SAI1 -- Peripheral
    SaiHandle::Config sai_config[2];

    sai_config[0].periph = SaiHandle::Config::Peripheral::SAI_1;
    sai_config[0].sr = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai_config[0].bit_depth = SaiHandle::Config::BitDepth::SAI_24BIT;
    sai_config[0].a_sync = SaiHandle::Config::Sync::MASTER;
    sai_config[0].b_sync = SaiHandle::Config::Sync::SLAVE;
    sai_config[0].pin_config.fs = Pin(PORTE, 4);
    sai_config[0].pin_config.mclk = Pin(PORTE, 2);
    sai_config[0].pin_config.sck = Pin(PORTE, 5);

    // Device-based Init
    switch (hw.seed.CheckBoardVersion()) {
        case DaisySeed::BoardVersion::DAISY_SEED_1_1:
            sai_config[0].a_dir = SaiHandle::Config::Direction::RECEIVE;
            sai_config[0].pin_config.sa = Pin(PORTE, 6);
            sai_config[0].b_dir = SaiHandle::Config::Direction::TRANSMIT;
            sai_config[0].pin_config.sb = Pin(PORTE, 3);
            break;
        default:
            sai_config[0].a_dir = SaiHandle::Config::Direction::TRANSMIT;
            sai_config[0].pin_config.sa = Pin(PORTE, 6);
            sai_config[0].b_dir = SaiHandle::Config::Direction::RECEIVE;
            sai_config[0].pin_config.sb = Pin(PORTE, 3);
            break;
    }

    // External Codec (SAI2)
    sai_config[1].periph = SaiHandle::Config::Peripheral::SAI_2;
    sai_config[1].sr = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai_config[1].bit_depth = SaiHandle::Config::BitDepth::SAI_24BIT;
    sai_config[1].a_sync = SaiHandle::Config::Sync::SLAVE;
    sai_config[1].b_sync = sai2ClockMaster ? SaiHandle::Config::Sync::MASTER : SaiHandle::Config::Sync::SLAVE;
    sai_config[1].a_dir = SaiHandle::Config::Direction::TRANSMIT;
    sai_config[1].b_dir = SaiHandle::Config::Direction::RECEIVE;

    sai_config[1].pin_config.mclk = seed::D24;
    sai_config[1].pin_config.sb = seed::D25;
    sai_config[1].pin_config.sa = seed::D26;
    sai_config[1].pin_config.fs = seed::D27;
    sai_config[1].pin_config.sck = seed::D28;

    SaiHandle sai_handle[2];
    sai_handle[0].Init(sai_config[0]);
    sai_handle[1].Init(sai_config[1]);

    hw.seed.audio_handle.Init(audioConfig, sai_handle[0], sai_handle[1]);

    // When SAI2 Audio Block B is set up to receive clock signal, it should be
    // in asynchronous mode
    if (!sai2ClockMaster) {
        SAI2_Block_B->CR1 &= ~SAI_xCR1_SYNCEN_Msk;
    }
}
