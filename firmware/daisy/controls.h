/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef CONTROLS_H
#define CONTROLS_H

#include "daisy_seed.h"
#include <cstdint>

using namespace daisy;

// Toggle switch values (0=left, 1=middle, 2=right)
extern int toggleValues[3];

// Switch change flags (set by UpdateSwitches, cleared by caller)
extern bool changed1;
extern bool changed2;
extern bool changed3;

// LED instances
extern Led led1, led2;

// Recording state
extern bool is_recording;
extern bool is_armed;

// Looper state
extern bool looper_enabled;
extern bool looper_recording;

// Initialize switch and LED hardware
void InitSwitches();
void InitLeds();

// Update switch state (call from main loop)
void UpdateSwitches();

// Update button state and handle footswitch actions
void UpdateButtons();

// Check for footswitch 1 long press
void CheckFsw1LongPress();

// Recording control
void StartRecording();
void StopRecording();
bool ArmRecording();
void DisarmRecording();
void UpdateArmBlinking();

// LED feedback functions
void ackLed();
void ackLed2();
void ackUartCommand();
void ackLedEffectIndex(int effectIndex);

// Non-blocking LED blink (set flag, processed in main loop)
void requestLedBlink();
void updateLedBlink();

// Utility
bool knobMoved(float old_value, float new_value);

#endif /* CONTROLS_H */
