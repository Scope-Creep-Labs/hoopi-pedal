/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "input_processing.h"

// Global instance
InputProcessor inputProcessor;

void InitInputProcessor(float samplerate) {
    inputProcessor.Init(samplerate);
}

void UpdateInputProcessorMode() {
    inputProcessor.SetMode(toggleValues[1]);
}
