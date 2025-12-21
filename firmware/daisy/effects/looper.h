/**
 * Hoopi Pedal - Effects module
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef LOOPER_H
#define LOOPER_H
#include "looper_module.h"

using namespace bkshepherd;

LooperModule *looperModule;
void InitLooper(float samplerate) {
    looperModule = new LooperModule();
    looperModule->Init(samplerate);
}

void UpdateLooperSwitches() {
    // Looper uses alternate footswitch for record trigger
    // Footswitch press: Toggle record on/off
    // Footswitch hold (1 second): Clear loop buffer
    // No toggle switch handling needed here
}

#endif /* LOOPER_H */
