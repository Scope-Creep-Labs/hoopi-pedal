/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "controls.h"
#include "hoopi_pedal.h"
#include "uart_protocol.h"
#include "effects/looper.h"

using namespace daisy;

// External hardware reference
extern HoopiPedal hw;
extern bool bypass;
extern uint8_t enabled_effect;
extern void EnableEffect(uint8_t newId);
extern void SendKnobValues();

// Toggle switch values
int toggleValues[3] = {0, 0, 0};

// Switch change flags
bool changed1 = false;
bool changed2 = false;
bool changed3 = false;

// LED instances
Led led1, led2;

// Recording state
bool is_recording = false;
bool is_armed = false;
uint32_t arm_blink_time = 0;
bool arm_led_state = false;

// Looper state
bool looper_enabled = false;

// Non-blocking LED blink state
static volatile bool ledBlinkRequested = false;
static uint32_t ledBlinkStartTime = 0;
static bool ledBlinkActive = false;
bool looper_recording = false;

// Internal switch state
static bool pswitch1[2], pswitch2[2], pswitch3[2];
static int switch1[2], switch2[2], switch3[2];

// Footswitch long-press detection
#define HOLD_THRESHOLD_MS 1000
static uint32_t footswitch1_start_time = 0;
static uint32_t footswitch2_start_time = 0;
static bool footswitch1_long_press_handled = false;
static bool footswitch2_long_press_handled = false;

void InitSwitches() {
    switch1[0] = HoopiPedal::SWITCH_1_LEFT;
    switch1[1] = HoopiPedal::SWITCH_1_RIGHT;
    switch2[0] = HoopiPedal::SWITCH_2_LEFT;
    switch2[1] = HoopiPedal::SWITCH_2_RIGHT;
    switch3[0] = HoopiPedal::SWITCH_3_LEFT;
    switch3[1] = HoopiPedal::SWITCH_3_RIGHT;

    // Force initial update by setting all to true
    pswitch1[0] = true;
    pswitch1[1] = true;
    pswitch2[0] = true;
    pswitch2[1] = true;
    pswitch3[0] = true;
    pswitch3[1] = true;
}

void InitLeds() {
    led1.Init(seed::D7, false);
    led1.Update();

    led2.Init(seed::D8, false);
    led2.Update();
}

void ackLed() {
    for (int i = 0; i < 5; i++) {
        led1.Set(1);
        led1.Update();
        hw.DelayMs(120);
        led1.Set(0);
        led1.Update();
        hw.DelayMs(120);
    }
    led1.Set(bypass ? 0.0f : 1.0f);
    led1.Update();
}

void ackUartCommand() {
    float prevState = bypass ? 0.0f : 1.0f;
    led1.Set(1.0f - prevState);  // Toggle to opposite state
    led1.Update();
    hw.DelayMs(30);
    led1.Set(prevState);  // Restore original state
    led1.Update();
}

void ackLedEffectIndex(int effectIndex) {
    int blinks = effectIndex + 1;
    for (int i = 0; i < blinks; i++) {
        led1.Set(1);
        led1.Update();
        hw.DelayMs(150);
        led1.Set(0);
        led1.Update();
        hw.DelayMs(150);
    }
    led1.Set(bypass ? 0.0f : 1.0f);
    led1.Update();
}

void requestLedBlink() {
    ledBlinkRequested = true;
}

void updateLedBlink() {
    uint32_t now = System::GetNow();

    if (ledBlinkRequested && !ledBlinkActive) {
        // Start blink - toggle LED to opposite of current state
        ledBlinkRequested = false;
        ledBlinkActive = true;
        ledBlinkStartTime = now;
        // If effect is active (LED on), turn off briefly; if bypassed (LED off), turn on briefly
        led1.Set(bypass ? 1.0f : 0.0f);
        led1.Update();
    }

    if (ledBlinkActive) {
        uint32_t elapsed = now - ledBlinkStartTime;
        if (elapsed >= 150) {
            // End blink after 150ms - restore to normal state
            ledBlinkActive = false;
            led1.Set(bypass ? 0.0f : 1.0f);
            led1.Update();
        }
    }
}

void ackLed2() {
    for (int i = 0; i < 5; i++) {
        led2.Set(1);
        led2.Update();
        hw.DelayMs(120);
        led2.Set(0);
        led2.Update();
        hw.DelayMs(120);
    }
}

bool knobMoved(float old_value, float new_value) {
    float tolerance = 0.005;
    return (new_value > (old_value + tolerance) || new_value < (old_value - tolerance));
}

void StartRecording() {
    if (!is_recording) {
        is_recording = true;
        led2.Set(1.0f);
        led2.Update();
        uint8_t data[] = {2};  // 2 = started
        UartSendFrame(0x01, data, 1);  // Recording Status (cmd=1)
    }
}

void StopRecording() {
    if (is_recording) {
        is_recording = false;
        led2.Set(0.0f);
        led2.Update();
        uint8_t data[] = {1};  // 1 = stopped
        UartSendFrame(0x01, data, 1);  // Recording Status (cmd=1)
    }
}

bool ArmRecording() {
    if (is_recording) {
        return false;
    }

    is_armed = true;
    arm_blink_time = System::GetNow();
    arm_led_state = true;
    led2.Set(1.0f);
    led2.Update();

    UartSendCmd(0x0A);  // Arm ACK
    return true;
}

void DisarmRecording() {
    if (is_armed) {
        is_armed = false;
        led2.Set(0.0f);
        led2.Update();
    }
}

void UpdateArmBlinking() {
    if (!is_armed) return;

    uint32_t now = System::GetNow();
    if (now - arm_blink_time >= 250) {
        arm_blink_time = now;
        arm_led_state = !arm_led_state;
        led2.Set(arm_led_state ? 1.0f : 0.0f);
        led2.Update();
    }
}

static bool CheckFsw2Pressed() {
    if (hw.switches[HoopiPedal::FOOTSWITCH_2].Pressed()) {
        if (footswitch2_start_time == 0) {
            footswitch2_start_time = System::GetNow();
            footswitch2_long_press_handled = false;
        } else if (System::GetNow() - footswitch2_start_time >= HOLD_THRESHOLD_MS) {
            if (!footswitch2_long_press_handled) {
                ackLed2();
                looperModule->AlternateFootswitchHeldFor1Second();
                footswitch2_long_press_handled = true;
            }
            return true;
        }
    } else {
        footswitch2_start_time = 0;
    }
    return false;
}

void UpdateButtons() {
    CheckFsw2Pressed();

    if (hw.switches[HoopiPedal::FOOTSWITCH_1].FallingEdge()) {
        if (toggleValues[2] == 0) {
            // Toggle looper enabled
            looper_enabled = !looper_enabled;
            led1.Set(looper_enabled ? 1.0f : 0.0f);
            led1.Update();
        } else if (toggleValues[2] == 1) {
            // Bypass toggle
            bypass = !bypass;
            if (!bypass) {
                ackLedEffectIndex(enabled_effect);
            } else {
                led1.Set(0.0f);
                led1.Update();
            }
            SendKnobValues();
        } else if (toggleValues[2] == 2) {
            // Next effect
            uint8_t next_effect = (enabled_effect + 1) % 8;  // EffectCount
            EnableEffect(next_effect);
        }
    } else if (hw.switches[HoopiPedal::FOOTSWITCH_2].FallingEdge()) {
        if (!footswitch2_long_press_handled) {
            if (toggleValues[2] == 0) {
                looperModule->AlternateFootswitchPressed();
                looper_recording = looperModule->isRecording;
                led2.Set(looper_recording ? 1.0f : 0.0f);
                led2.Update();
            } else {
                if (is_armed) {
                    is_armed = false;
                    StartRecording();
                } else if (!is_recording) {
                    StartRecording();
                } else {
                    StopRecording();
                }
            }
        }
        footswitch2_long_press_handled = false;
    }
}

void UpdateSwitches() {
    // 3-way Switch 1
    changed1 = false;
    for (int i = 0; i < 2; i++) {
        if (hw.switches[switch1[i]].Pressed() != pswitch1[i]) {
            pswitch1[i] = hw.switches[switch1[i]].Pressed();
            changed1 = true;
        }
    }
    if (changed1) {
        if (pswitch1[0] == true) {
            toggleValues[0] = 2;
        } else if (pswitch1[1] == true) {
            toggleValues[0] = 0;
        } else {
            toggleValues[0] = 1;
        }
    }

    // 3-way Switch 2
    changed2 = false;
    for (int i = 0; i < 2; i++) {
        if (hw.switches[switch2[i]].Pressed() != pswitch2[i]) {
            pswitch2[i] = hw.switches[switch2[i]].Pressed();
            changed2 = true;
        }
    }
    if (changed2) {
        if (pswitch2[0] == true) {
            toggleValues[1] = 2;
        } else if (pswitch2[1] == true) {
            toggleValues[1] = 0;
        } else {
            toggleValues[1] = 1;
        }
    }

    // 3-way Switch 3
    changed3 = false;
    for (int i = 0; i < 2; i++) {
        if (hw.switches[switch3[i]].Pressed() != pswitch3[i]) {
            pswitch3[i] = hw.switches[switch3[i]].Pressed();
            changed3 = true;
        }
    }
    if (changed3) {
        if (pswitch3[0] == true) {
            toggleValues[2] = 2;
            led1.Set(looper_enabled ? 1.0f : 0.0f);
            led1.Update();
        } else if (pswitch3[1] == true) {
            toggleValues[2] = 0;
        } else {
            toggleValues[2] = 1;
            led1.Set(bypass ? 0.0f : 1.0f);
            led1.Update();
        }
    }
}

void CheckFsw1LongPress() {
    if (hw.switches[HoopiPedal::FOOTSWITCH_1].Pressed()) {
        if (footswitch1_start_time == 0) {
            footswitch1_start_time = System::GetNow();
            footswitch1_long_press_handled = false;
        } else if (System::GetNow() - footswitch1_start_time >= HOLD_THRESHOLD_MS) {
            if (!footswitch1_long_press_handled) {
                uint8_t next_effect = (enabled_effect + 1) % 8;  // EffectCount
                enabled_effect = next_effect;
                bypass = false;
                ackLedEffectIndex(next_effect);

                uint8_t data[] = {next_effect};
                UartSendFrame(0x07, data, 1);  // Effect Switched

                footswitch1_long_press_handled = true;
            }
        }
    } else {
        footswitch1_start_time = 0;
    }
}
