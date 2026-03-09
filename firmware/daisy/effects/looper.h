/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef LOOPER_H
#define LOOPER_H
#include "looper_module.h"

using namespace bkshepherd;

extern LooperModule *looperModule;
void InitLooper(float samplerate);
void UpdateLooperSwitches();

#endif /* LOOPER_H */
