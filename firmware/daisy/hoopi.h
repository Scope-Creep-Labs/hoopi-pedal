/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef HOOPI_H
#define HOOPI_H

#include "hoopi_pedal.h"
#include "daisysp.h"
#include "pin_defs.h"
#include "audio_config.h"
#include "uart_protocol.h"
#include "uart_commands.h"
#include "controls.h"
#include "ota.h"

// Hardware instance
extern HoopiPedal hw;

// Configure twin (4-channel) audio I/O
void ConfigureTwinAudio(daisy::AudioHandle::Config audioConfig, bool sai2ClockMaster);

#endif /* HOOPI_H */
