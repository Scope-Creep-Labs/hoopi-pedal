/**
 * Hoopi Pedal - Pin definitions
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef BBD01BDE_21C9_493A_A478_9A9CD6E958F2
#define BBD01BDE_21C9_493A_A478_9A9CD6E958F2

#include <string.h>
#include "daisy_seed.h"
// #include <daisysp.h>
using namespace daisy;

const daisy::Pin SAI2_I2S_MCLK = seed::D24;
const daisy::Pin SAI2_I2S_DIN = seed::D25;
const daisy::Pin SAI2_I2S_DOUT = seed::D26;
const daisy::Pin SAI2_I2S_FS = seed::D27;
const daisy::Pin SAI2_I2S_SCK = seed::D28;

const daisy::Pin I2C_SCL = seed::D11;
const daisy::Pin I2C_SDA = seed::D12;

const daisy::Pin UART_RX = seed::D11;
const daisy::Pin UART_TX = seed::D12;

const daisy::Pin PEDAL_FSW1 = seed::D13;
const daisy::Pin PEDAL_FSW2 = seed::D14;

const daisy::Pin PEDAL_SW1_UP = seed::D1;
const daisy::Pin PEDAL_SW1_DOWN = seed::D2;
const daisy::Pin PEDAL_SW2_UP = seed::D3;
const daisy::Pin PEDAL_SW2_DOWN = seed::D4;
const daisy::Pin PEDAL_SW3_UP = seed::D5;
const daisy::Pin PEDAL_SW3_DOWN = seed::D6;

const daisy::Pin PEDAL_LED1 = seed::D7;
const daisy::Pin PEDAL_LED2 = seed::D8;

const daisy::Pin PEDAL_POT1 = seed::A1;
const daisy::Pin PEDAL_POT2 = seed::A2;
const daisy::Pin PEDAL_POT3 = seed::A3;
const daisy::Pin PEDAL_POT4 = seed::A4;
const daisy::Pin PEDAL_POT5 = seed::A5;
const daisy::Pin PEDAL_POT6 = seed::A6;
#endif /* BBD01BDE_21C9_493A_A478_9A9CD6E958F2 */

