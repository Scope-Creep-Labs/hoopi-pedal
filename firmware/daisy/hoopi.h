/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef AE5761B6_2132_4609_9EE7_AC848C0F56B0
#define AE5761B6_2132_4609_9EE7_AC848C0F56B0

#include "hoopi_pedal.h"
#include "daisysp.h"
#include "pin_defs.h"
#include "effects/looper.h"

using namespace daisy;
using namespace daisysp;

HoopiPedal hw;

enum Effect {
  Galaxy,
  Reverb,
  AmpSim,
  NamSim,
  Distortion,
  Delay,
  Tremolo,
  ChorusSim,
  EffectCount  // Keep this last - used for range validation
};

// Global audio config (settable via UART, shared across all effects)
uint8_t outputBlendMode = 1;        // 0=stereo, 1=mono center, 2=mono L, 3=mono R, 4-255=blend ratio
uint8_t applyBlendToRecording = 0;  // 0=recording stays stereo, 1=apply blend to recording outputs too
uint8_t galaxyLiteDamping = 140;    // 0-255 maps to 0.0-1.0 (default 140 ≈ 0.55)
uint8_t galaxyLitePreDelay = 128;   // 0-255 maps to 0.0-1.0 (default 128 ≈ 0.5)
uint8_t galaxyLiteMix = 77;         // 0-255 maps to 0.0-1.0 (default 77 ≈ 0.3)

// Compressor parameters (param_id 4-8)
uint8_t compThreshold = 102;        // 0-255 maps to -60dB to 0dB (default 102 ≈ -20dB)
uint8_t compRatio = 40;             // 0-255 maps to 1:1 to 20:1 (default 40 ≈ 4:1)
uint8_t compAttack = 5;             // 0-255 maps to 1ms to 500ms (default 5 ≈ 10ms)
uint8_t compRelease = 12;           // 0-255 maps to 10ms to 2000ms (default 12 ≈ 100ms)
uint8_t compMakeupGain = 85;        // 0-255 maps to 1.0x to 4.0x (default 85 ≈ 1.5x)

// Noise Gate parameters (param_id 9-12)
uint8_t gateThreshold = 128;        // 0-255 maps to -80dB to -20dB (default 128 ≈ -50dB)
uint8_t gateAttack = 5;             // 0-255 maps to 0.1ms to 50ms (default 5 ≈ 1ms)
uint8_t gateHold = 26;              // 0-255 maps to 0ms to 500ms (default 26 ≈ 50ms)
uint8_t gateRelease = 12;           // 0-255 maps to 10ms to 2000ms (default 12 ≈ 100ms)

// 3-Band EQ parameters (param_id 30-36) - disabled by default
// Note: Using param_id 30-36 to avoid conflicts with effect MIDI CC mappings (14-29)
uint8_t eqEnabled = 0;              // param_id 30: 0 = disabled, 1 = enabled
uint8_t eqLowGain = 128;            // param_id 31: 0-255 maps to -12dB to +12dB (128 = unity/0dB)
uint8_t eqMidGain = 128;            // param_id 32: 0-255 maps to -12dB to +12dB (128 = unity/0dB)
uint8_t eqHighGain = 128;           // param_id 33: 0-255 maps to -12dB to +12dB (128 = unity/0dB)
uint8_t eqLowFreq = 85;             // param_id 34: 0-255 maps to 50Hz to 500Hz (default 85 ≈ 200Hz)
uint8_t eqMidFreq = 51;             // param_id 35: 0-255 maps to 250Hz to 4kHz (default 51 ≈ 1kHz)
uint8_t eqHighFreq = 64;            // param_id 36: 0-255 maps to 2kHz to 10kHz (default 64 ≈ 4kHz)

// Forward declarations for effect modules (defined in effect headers)
namespace bkshepherd { class BaseEffectModule; }

// Get effect module pointer by effect index (implemented in hoopi.cpp)
bkshepherd::BaseEffectModule* GetEffectModule(uint8_t effectIdx);

Effect enabled_effect;

bool            pswitch1[2], pswitch2[2], pswitch3[2];
int             switch1[2], switch2[2], switch3[2];

float knobValues[6]; // Moved to global
int toggleValues[3] = {0, 0, 0};

Led led1, led2;

// Apply output blend based on global blend mode setting
// Recording outputs (out[2]/out[3]) always remain stereo
inline void ApplyOutputBlend(float inL, float inR, float& outL, float& outR) {
    if (outputBlendMode == 0) {
        // Stereo: L/R stay separate
        outL = inL;
        outR = inR;
    } else if (outputBlendMode == 1) {
        // Mono center: 50/50 blend to both outputs
        float mono = (inL + inR) * 0.5f;
        outL = mono;
        outR = mono;
    } else if (outputBlendMode == 2) {
        // Mono left: blend both to L, R silent
        outL = (inL + inR) * 0.5f;
        outR = 0.0f;
    } else if (outputBlendMode == 3) {
        // Mono right: blend both to R, L silent
        outL = 0.0f;
        outR = (inL + inR) * 0.5f;
    } else {
        // Blend ratio: 4-255 controls L/R mix ratio in mono output
        // 4=100% L, 128=50/50, 255=100% R
        float ratioR = (outputBlendMode - 4) / 251.0f;  // 0.0 to 1.0
        float ratioL = 1.0f - ratioR;
        float mono = inL * ratioL + inR * ratioR;
        outL = mono;
        outR = mono;
    }
}

bool            bypass;
bool is_recording = false;
bool is_armed = false;           // Recording armed, waiting for footswitch
uint32_t arm_blink_time = 0;     // For LED blink timing when armed
bool arm_led_state = false;      // Current state of blinking LED
bool looper_enabled = false;
bool looper_recording = false;

uint8_t seed_fw_version = 7;  // Bumped to verify new firmware is running

UartHandler uart;

// UART v2 Protocol Constants
#define UART_START_BYTE 0xAA
#define UART_MAX_DATA_LEN 8    // CMD + up to 7 data bytes (knob values needs 7 data)
#define UART_MAX_PACKET_LEN 11 // START + LEN + CMD + 7 DATA + CHECKSUM
#define UART_RX_TIMEOUT_MS 100

// Buffer sizes
#define UART_SEND_BUFF_SIZE 11  // Max packet size
#define UART_RCV_BUFF_SIZE 16   // DMA receive buffer

static uint8_t DMA_BUFFER_MEM_SECTION send_buffer[UART_SEND_BUFF_SIZE];
static uint8_t DMA_BUFFER_MEM_SECTION dma_recv_buffer[UART_RCV_BUFF_SIZE];  // DMA writes here
static uint8_t recv_buffer[UART_RCV_BUFF_SIZE];  // Main loop reads from here (copy)
static uint8_t rx_payload[UART_MAX_DATA_LEN];    // Parsed command + data
static uint8_t rx_payload_len = 0;
bool uart_data_recv = false;

// Calculate XOR checksum over LEN and payload bytes
uint8_t UartCalculateChecksum(uint8_t len, uint8_t* payload) {
    uint8_t checksum = len;
    for (int i = 0; i < len; i++) {
        checksum ^= payload[i];
    }
    return checksum;
}

// Send a framed packet: START + LEN + CMD + DATA + CHECKSUM
void UartSendFrame(uint8_t cmd, uint8_t* data, uint8_t data_len) {
    uint8_t len = 1 + data_len;  // CMD + DATA
    uint8_t payload[UART_MAX_DATA_LEN];

    payload[0] = cmd;
    for (int i = 0; i < data_len && i < UART_MAX_DATA_LEN - 1; i++) {
        payload[i + 1] = data[i];
    }

    send_buffer[0] = UART_START_BYTE;
    send_buffer[1] = len;
    for (int i = 0; i < len; i++) {
        send_buffer[2 + i] = payload[i];
    }
    send_buffer[2 + len] = UartCalculateChecksum(len, payload);

    int total_len = 3 + len;  // START + LEN + payload + CHECKSUM
    uart.BlockingTransmit(send_buffer, total_len);
}

// Send a simple command with no data
void UartSendCmd(uint8_t cmd) {
    UartSendFrame(cmd, nullptr, 0);
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

// Quick single blink to acknowledge UART command received
void ackUartCommand() {
  float prevState = bypass ? 0.0f : 1.0f;
  led1.Set(1.0f - prevState);  // Toggle to opposite state
  led1.Update();
  hw.DelayMs(30);
  led1.Set(prevState);  // Restore original state
  led1.Update();
}

// Blink LED1 to indicate effect index (blinks = index + 1)
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


// UART RX state machine states
enum UartRxState {
    UART_RX_WAIT_START,
    UART_RX_WAIT_LEN,
    UART_RX_READ_PAYLOAD,
    UART_RX_WAIT_CHECKSUM
};

static UartRxState uart_rx_state = UART_RX_WAIT_START;
static uint8_t uart_rx_len = 0;
static uint8_t uart_rx_idx = 0;
static uint32_t uart_rx_start_time = 0;

// Ring buffer for DMA received data
#define UART_RING_SIZE 64
static uint8_t uart_ring_buffer[UART_RING_SIZE];
static volatile uint16_t uart_ring_head = 0;  // Written by DMA callback
static uint16_t uart_ring_tail = 0;           // Read by main loop

// DMA circular receive callback - called on half/full transfer and idle
void UartDmaCallback(uint8_t* data, size_t size, void* context, UartHandler::Result result) {
    if (result == UartHandler::Result::OK && size > 0) {
        // Copy received bytes to ring buffer
        for (size_t i = 0; i < size; i++) {
            uint16_t next_head = (uart_ring_head + 1) % UART_RING_SIZE;
            if (next_head != uart_ring_tail) {  // Don't overflow
                uart_ring_buffer[uart_ring_head] = data[i];
                uart_ring_head = next_head;
            }
        }
    }
}

void InitUart() {
     // Configure the Uart Peripheral
     UartHandler::Config uart_conf;
     uart_conf.periph        = UartHandler::Config::Peripheral::UART_4;
     uart_conf.mode          = UartHandler::Config::Mode::TX_RX;
     uart_conf.pin_config.tx = UART_TX;
     uart_conf.pin_config.rx = UART_RX;

     // Clear buffers
     for (int i = 0; i < UART_RCV_BUFF_SIZE; i++) {
         dma_recv_buffer[i] = 0;
         recv_buffer[i] = 0;
     }
     for (int i = 0; i < UART_SEND_BUFF_SIZE; i++) {
         send_buffer[i] = 0;
     }
     for (int i = 0; i < UART_MAX_DATA_LEN; i++) {
         rx_payload[i] = 0;
     }
     uart_ring_head = 0;
     uart_ring_tail = 0;

     uart.Init(uart_conf);
     uart_data_recv = false;
     uart_rx_state = UART_RX_WAIT_START;

     // Start DMA listening mode for continuous reception
     uart.DmaListenStart(dma_recv_buffer, UART_RCV_BUFF_SIZE, UartDmaCallback, nullptr);
}

// Check if ring buffer has data available
inline bool UartRingAvailable() {
    return uart_ring_head != uart_ring_tail;
}

// Read one byte from ring buffer (call only if UartRingAvailable() is true)
inline uint8_t UartRingRead() {
    uint8_t byte = uart_ring_buffer[uart_ring_tail];
    uart_ring_tail = (uart_ring_tail + 1) % UART_RING_SIZE;
    return byte;
}

// Poll for UART data and parse framed packets
// Returns true if a complete valid packet was received
bool UartPollReceive() {
    uint8_t byte;

    // Check for timeout - reset state if we've been waiting too long
    if (uart_rx_state != UART_RX_WAIT_START) {
        if (System::GetNow() - uart_rx_start_time > UART_RX_TIMEOUT_MS) {
            uart_rx_state = UART_RX_WAIT_START;
        }
    }

    // Process all available bytes from ring buffer
    while (UartRingAvailable()) {
        byte = UartRingRead();

        switch (uart_rx_state) {
            case UART_RX_WAIT_START:
                if (byte == UART_START_BYTE) {
                    uart_rx_state = UART_RX_WAIT_LEN;
                    uart_rx_start_time = System::GetNow();
                }
                break;

            case UART_RX_WAIT_LEN:
                if (byte >= 1 && byte <= 8) {  // Valid LEN range (1-8 bytes)
                    uart_rx_len = byte;
                    uart_rx_idx = 0;
                    uart_rx_state = UART_RX_READ_PAYLOAD;
                } else {
                    // Invalid length, reset
                    uart_rx_state = UART_RX_WAIT_START;
                }
                break;

            case UART_RX_READ_PAYLOAD:
                rx_payload[uart_rx_idx++] = byte;
                if (uart_rx_idx >= uart_rx_len) {
                    uart_rx_state = UART_RX_WAIT_CHECKSUM;
                }
                break;

            case UART_RX_WAIT_CHECKSUM:
                {
                    uint8_t expected = UartCalculateChecksum(uart_rx_len, rx_payload);
                    if (byte == expected) {
                        // Valid packet received
                        rx_payload_len = uart_rx_len;
                        uart_rx_state = UART_RX_WAIT_START;
                        return true;
                    } else {
                        // Checksum mismatch, discard
                        uart_rx_state = UART_RX_WAIT_START;
                    }
                }
                break;
        }
    }

    return false;
}


void EnableEffect(uint8_t newId) {
    // Validate the ID is within valid range
    if (newId < EffectCount) {
        enabled_effect = static_cast<Effect>(newId);
    } else {
        // Default to Galaxy if invalid ID
        enabled_effect = Effect::Galaxy;
        newId = 0;
    }
    uint8_t data[] = {newId};
    UartSendFrame(0x07, data, 1);  // Effect Switched (cmd=7)
}

bool knobMoved(float old_value, float new_value)
{
  float tolerance = 0.005;
  if (new_value > (old_value + tolerance) || new_value < (old_value - tolerance))
  {
    return true;
  }
  else
  {
    return false;
  }
}

void InitSwitches() {

    switch1[0]= HoopiPedal::SWITCH_1_LEFT;
    switch1[1]= HoopiPedal::SWITCH_1_RIGHT;
    switch2[0]= HoopiPedal::SWITCH_2_LEFT;
    switch2[1]= HoopiPedal::SWITCH_2_RIGHT;
    switch3[0]= HoopiPedal::SWITCH_3_LEFT;
    switch3[1]= HoopiPedal::SWITCH_3_RIGHT;

    pswitch1[0]= true; // TODO I think by setting all these to true, will force loading the correct model/ir on booting the pedal - verify
    pswitch1[1]= true;
    pswitch2[0]= true;
    pswitch2[1]= true;
    pswitch3[0]= true;
    pswitch3[1]= true;

}

void InitLeds() {
    led1.Init(seed::D7,false);
    led1.Update();

    led2.Init(seed::D8,false);
    led2.Update();
}

void StartRecording() {
  if (!is_recording)
  {
    is_recording = true;
    led2.Set(is_recording ? 1.0 : 0.0);
    led2.Update();
    uint8_t data[] = {2};  // 2 = started
    UartSendFrame(0x01, data, 1);  // Recording Status (cmd=1)
  }
}

void SendFWVersion() {
  uint8_t data[] = {seed_fw_version};
  UartSendFrame(0x04, data, 1);  // Firmware Version (cmd=4)
}

void SendToggleValues() {
  uint8_t data[] = {
    static_cast<uint8_t>(toggleValues[0]),
    static_cast<uint8_t>(toggleValues[1]),
    static_cast<uint8_t>(toggleValues[2])
  };
  UartSendFrame(0x06, data, 3);  // Toggle Values (cmd=6)
}

void SendKnobValues() {
  hw.ProcessAnalogControls();  // Update ADC readings
  uint8_t data[7];
  for (int i = 0; i < 6; i++) {
    float val = hw.GetKnobValue(static_cast<HoopiPedal::Knob>(i));
    data[i] = static_cast<uint8_t>(val * 255.0f);
  }
  // Pack effect (lower 4 bits) and toggle switch (upper 4 bits) into byte7
  data[6] = static_cast<uint8_t>(enabled_effect) | (static_cast<uint8_t>(toggleValues[1]) << 4);
  UartSendFrame(0x09, data, 7);  // Knob Values (cmd=9)
}

void StopRecording() {
  if (is_recording)
  {
    is_recording = false;
    led2.Set(is_recording ? 1.0 : 0.0);
    led2.Update();
    uint8_t data[] = {1};  // 1 = stopped
    UartSendFrame(0x01, data, 1);  // Recording Status (cmd=1)
  }
}

// Arm recording - returns true if successfully armed
bool ArmRecording() {
  // Don't arm if already recording
  if (is_recording) {
    return false;
  }

  is_armed = true;
  arm_blink_time = System::GetNow();
  arm_led_state = true;
  led2.Set(1.0f);  // Start with LED on
  led2.Update();

  // Send Arm ACK (cmd=0x0A)
  UartSendCmd(0x0A);
  return true;
}

// Called from main loop to update LED blinking when armed
void UpdateArmBlinking() {
  if (!is_armed) return;

  uint32_t now = System::GetNow();
  // Blink every 250ms (4Hz blink rate)
  if (now - arm_blink_time >= 250) {
    arm_blink_time = now;
    arm_led_state = !arm_led_state;
    led2.Set(arm_led_state ? 1.0f : 0.0f);
    led2.Update();
  }
}

// Cancel armed state (e.g., if user stops it before pressing footswitch)
void DisarmRecording() {
  if (is_armed) {
    is_armed = false;
    led2.Set(0.0f);
    led2.Update();
  }
}

#define HOLD_THRESHOLD_MS 1000

uint32_t footswitch1_start_time = 0;
uint32_t footswitch2_start_time = 0;
bool footswitch1_long_press_handled = false;
bool footswitch2_long_press_handled = false;

bool CheckFsw2Pressed() {
  if (hw.switches[HoopiPedal::FOOTSWITCH_2].Pressed()) {
    if (footswitch2_start_time == 0) {
      footswitch2_start_time = System::GetNow();
      footswitch2_long_press_handled = false; // Reset flag on new press
    } else if (System::GetNow() - footswitch2_start_time >=
               HOLD_THRESHOLD_MS) {
      if (!footswitch2_long_press_handled) {
        ackLed2();
        looperModule->AlternateFootswitchHeldFor1Second();
        footswitch2_long_press_handled = true; // Mark as handled
      }
      return true;
    }
  } else {
    // Reset the hold timer and flag when footswitch is released
    footswitch2_start_time = 0;
    // Don't reset the flag here - let FallingEdge check it first
  }
  return false;
}



void UpdateButtons()
{
    CheckFsw2Pressed();

    if(hw.switches[HoopiPedal::FOOTSWITCH_1].FallingEdge())
    {
            //  Don't ever, for any reason, do anything to anyone for any reason ever, no matter what, no matter where, or who, or who you are with, or where you are going, or where you've been... ever, for any reason whatsoever...

            // left toggle switch determines footswitch 1 behavior:
            // Left (0): enable/disable looper
            // Middle (1): Enable/disable effect (bypass toggle)
            // Right (2): Next effect (increment ID)
            if (toggleValues[2] == 0) {
                // Previous effect (with rollover to last effect)
                // Always bypass when switching
                // bypass = true;
                // led1.Set(0.0f);
                // led1.Update();
                //
                // uint8_t prev_effect = (enabled_effect == 0) ? (EffectCount - 1) : (enabled_effect - 1);
                // EnableEffect(prev_effect);
              looper_enabled = !looper_enabled;
              led1.Set(looper_enabled ? 1.0f : 0.0f);
              led1.Update();
            }
            else if (toggleValues[2] == 1) {
                // Bypass toggle (enable/disable effect)
                bypass = !bypass;
                if (!bypass) {
                    // When enabling effect, blink to indicate current effect then turn on
                    ackLedEffectIndex(enabled_effect);
                } else {
                    led1.Set(0.0f);
                    led1.Update();
                }
            }
            else if (toggleValues[2] == 2) {
                // Next effect (with rollover to 0)
                // // Always bypass when switching
                // bypass = true;
                // led1.Set(0.0f);
                // led1.Update();

                uint8_t next_effect = (enabled_effect + 1) % EffectCount;
                EnableEffect(next_effect);
            }
    }

    else if(hw.switches[HoopiPedal::FOOTSWITCH_2].FallingEdge())
    {
      //  Don't ever, for any reason, do anything to anyone for any reason ever, no matter what, no matter where, or who, or who you are with, or where you are going, or where you've been... ever, for any reason whatsoever...

      // Only handle if long press wasn't triggered
      if (!footswitch2_long_press_handled) {

        //left toggle determines if this is a looper action or sd card recording action
        // left = looper
        if (toggleValues[2] == 0)
        {
          looperModule->AlternateFootswitchPressed();
          looper_recording = looperModule->isRecording;
          led2.Set(looper_recording ? 1.0 : 0.0);
          led2.Update();
        }
        else {
          // If armed, pressing footswitch starts recording
          if (is_armed) {
            is_armed = false;  // Clear armed state
            StartRecording();  // This will turn LED solid on
          }
          else if (!is_recording)
          {
            StartRecording();
          }
          else
          {
            StopRecording();
          }
        }
      }
      // Reset flag after handling (or not handling) the falling edge
      footswitch2_long_press_handled = false;
    }

}

bool changed1 = false;
bool changed2 = false;
bool changed3 = false;

void UpdateSwitches()
{
    // Detect any changes in switch positions
    // left is 0
    // middle position is 1
    //right is 2

    // 3-way Switch 1
    changed1 = false;
    for(int i=0; i<2; i++) {
        if (hw.switches[switch1[i]].Pressed() != pswitch1[i]) {
            pswitch1[i] = hw.switches[switch1[i]].Pressed();
            changed1 = true;
        }
    }
    if (changed1)
    { // update_switches is for turning off preset
      // changed1 = true;
      if (pswitch1[0] == true)
      {
        toggleValues[0] = 2;
      }
      else if (pswitch1[1] == true)
      {
        toggleValues[0] = 0;
      }
      else
      {
        toggleValues[0] = 1;
      }
    }

    // 3-way Switch 2
    changed2 = false;
    for(int i=0; i<2; i++) {
        if (hw.switches[switch2[i]].Pressed() != pswitch2[i]) {
            pswitch2[i] = hw.switches[switch2[i]].Pressed();
            changed2 = true;
        }
    }
    if (changed2) {
      // changed2 = true;
      if (pswitch2[0] == true)
      {
        toggleValues[1] = 2;
      }
      else if (pswitch2[1] == true)
      {
        toggleValues[1] = 0;
      }
      else
      {
        toggleValues[1] = 1;
      }
    }

    // 3-way Switch 3
    changed3 = false;
    for(int i=0; i<2; i++) {
        if (hw.switches[switch3[i]].Pressed() != pswitch3[i]) {
            pswitch3[i] = hw.switches[switch3[i]].Pressed();
            changed3 = true;
        }
    }
    if (changed3) {

      if (pswitch3[0] == true)
      {
        toggleValues[2] = 2;
        led1.Set(looper_enabled ? 1.0f : 0.0f);
        led1.Update();
      }
      else if (pswitch3[1] == true)
      {
        toggleValues[2] = 0;
      }
      else
      {
        toggleValues[2] = 1;
        led1.Set(bypass ? 0.0f : 1.0f);
        led1.Update();
      }

    }
}

void ConfigureTwinAudio(AudioHandle::Config audioConfig,
  bool sai2ClockMaster)
{
    // SAI1 -- Peripheral
    // Configure
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
    switch (hw.seed.CheckBoardVersion())
    {
    case DaisySeed::BoardVersion::DAISY_SEED_1_1:
    {
        // Data Line Directions
        sai_config[0].a_dir = SaiHandle::Config::Direction::RECEIVE;
        sai_config[0].pin_config.sa = Pin( PORTE, 6 );
        sai_config[0].b_dir = SaiHandle::Config::Direction::TRANSMIT;
        sai_config[0].pin_config.sb = Pin( PORTE, 3 );
    }
    break;
    default:
    {
        // Data Line Directions
        sai_config[0].a_dir = SaiHandle::Config::Direction::TRANSMIT;
        sai_config[0].pin_config.sa = Pin( PORTE, 6 );
        sai_config[0].b_dir = SaiHandle::Config::Direction::RECEIVE;
        sai_config[0].pin_config.sb = Pin( PORTE, 3 );
    }
    break;
    }

    // External Codec
    sai_config[1].periph = SaiHandle::Config::Peripheral::SAI_2;
    sai_config[1].sr = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai_config[1].bit_depth = SaiHandle::Config::BitDepth::SAI_24BIT;
    // There is no use making Audio Block A a master, since none of Daisy Seed's
    // pins has those functions (FS, SCK, MCLK).
    sai_config[1].a_sync = SaiHandle::Config::Sync::SLAVE;
    // but Audio Block B *can* be MASTER
    sai_config[1].b_sync = (sai2ClockMaster) ? SaiHandle::Config::Sync::MASTER : SaiHandle::Config::Sync::SLAVE;
    // if we want to use the AudioCallback, I think we need to keep these as is,
    // but in theory we could have two stereo outputs or two stereo inputs.
    sai_config[1].a_dir = SaiHandle::Config::Direction::TRANSMIT;
    sai_config[1].b_dir = SaiHandle::Config::Direction::RECEIVE;

    sai_config[1].pin_config.mclk = seed::D24;
    sai_config[1].pin_config.sb = seed::D25;
    sai_config[1].pin_config.sa = seed::D26;
    sai_config[1].pin_config.fs = seed::D27;
    sai_config[1].pin_config.sck = seed::D28;

    SaiHandle sai_handle[2];

    // Then Initialize
    sai_handle[0].Init(sai_config[0]);
    sai_handle[1].Init(sai_config[1]);

    // Reinit Audio for _both_ codecs...
    hw.seed.audio_handle.Init(audioConfig, sai_handle[0], sai_handle[1]);

    // When SAI2 Audio Block B is set up to receive clock signal, it should be
    // in asynchronous mode (SCK and FS are inputs in this case).
    // SaiHandle::Init assumes it is synchronous, so we have to fix it here.
    if (!(sai2ClockMaster))
    {
        // SYNCEN[1:0]=00 means ASYNC (master/slave)
        SAI2_Block_B->CR1 &= ~SAI_xCR1_SYNCEN_Msk;
    }
}

void CheckFsw1LongPress() {
  if (hw.switches[HoopiPedal::FOOTSWITCH_1].Pressed()) {
    if (footswitch1_start_time == 0) {
      footswitch1_start_time = System::GetNow();
      footswitch1_long_press_handled = false;
    } else if (System::GetNow() - footswitch1_start_time >=
               HOLD_THRESHOLD_MS) {
      if (!footswitch1_long_press_handled) {
        // Go to next effect (with rollover to first)
        uint8_t next_effect = (enabled_effect + 1) % EffectCount;
        enabled_effect = static_cast<Effect>(next_effect);

        // Enable the effect (disable bypass)
        bypass = false;

        // Blink LED to indicate effect index (blinks = index + 1), then turn on
        ackLedEffectIndex(next_effect);

        // Notify via UART
        uint8_t data[] = {next_effect};
        UartSendFrame(0x07, data, 1);  // Effect Switched (cmd=7)

        footswitch1_long_press_handled = true;
      }
    }
  } else {
    // Reset the hold timer when footswitch is released
    footswitch1_start_time = 0;
  }
}



#endif /* AE5761B6_2132_4609_9EE7_AC848C0F56B0 */

