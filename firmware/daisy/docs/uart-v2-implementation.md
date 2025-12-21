# UART Protocol v2 Implementation - Daisy Seed

This document details the implementation of the UART v2 framed protocol on the Daisy Seed side, matching the ESP32's existing v2 implementation.

## Overview

The v2 protocol adds packet framing with a start byte and XOR checksum for reliable communication between ESP32 and Daisy Seed.

### Packet Format

```
+-------+-----+-----+----------+----------+
| START | LEN | CMD | DATA ... | CHECKSUM |
+-------+-----+-----+----------+----------+
   1B     1B    1B    0-5 bytes    1B
```

| Field    | Size    | Description                                      |
|----------|---------|--------------------------------------------------|
| START    | 1 byte  | Sync byte, always `0xAA`                         |
| LEN      | 1 byte  | Payload length (CMD + DATA bytes, 1-6)           |
| CMD      | 1 byte  | Command identifier                               |
| DATA     | 0-5 B   | Command-specific payload                         |
| CHECKSUM | 1 byte  | XOR of all bytes from LEN through last DATA byte |

---

## Code Changes

### 1. Protocol Constants (`hoopi.h`)

Added constants for packet structure and timeout handling:

```cpp
// UART v2 Protocol constants
#define UART_START_BYTE 0xAA
#define UART_MAX_DATA_LEN 7      // CMD + up to 6 data bytes
#define UART_MAX_PACKET_LEN 9    // START + LEN + CMD + 5 DATA + CHECKSUM
#define UART_RX_TIMEOUT_MS 100   // Reset state if packet incomplete after 100ms
```

### 2. RX State Machine (`hoopi.h`)

Added state machine for parsing incoming framed packets:

```cpp
// RX state machine
enum UartRxState {
    UART_RX_WAIT_START,     // Waiting for 0xAA start byte
    UART_RX_WAIT_LEN,       // Waiting for length byte
    UART_RX_READ_PAYLOAD,   // Reading CMD + DATA bytes
    UART_RX_WAIT_CHECKSUM   // Waiting for checksum byte
};

// State variables
UartRxState uart_rx_state = UART_RX_WAIT_START;
uint8_t uart_rx_len = 0;           // Expected payload length
uint8_t uart_rx_idx = 0;           // Current index in payload
uint32_t uart_rx_start_time = 0;   // For timeout detection
uint8_t rx_payload[UART_MAX_DATA_LEN];  // Received payload buffer
uint8_t rx_payload_len = 0;        // Actual received payload length
```

### 3. Checksum Calculation (`hoopi.h`)

XOR checksum of LEN byte and all payload bytes:

```cpp
uint8_t UartCalculateChecksum(uint8_t len, uint8_t* payload) {
    uint8_t checksum = len;
    for (int i = 0; i < len; i++) {
        checksum ^= payload[i];
    }
    return checksum;
}
```

### 4. TX Functions (`hoopi.h`)

#### `UartSendFrame()` - Send framed packet with data

```cpp
void UartSendFrame(uint8_t cmd, uint8_t* data, uint8_t data_len) {
    uint8_t len = 1 + data_len;  // CMD + DATA
    uint8_t payload[UART_MAX_DATA_LEN];
    payload[0] = cmd;
    for (int i = 0; i < data_len && i < UART_MAX_DATA_LEN - 1; i++) {
        payload[i + 1] = data[i];
    }

    send_buffer[0] = UART_START_BYTE;  // 0xAA
    send_buffer[1] = len;
    for (int i = 0; i < len; i++) {
        send_buffer[2 + i] = payload[i];
    }
    send_buffer[2 + len] = UartCalculateChecksum(len, payload);

    int total_len = 3 + len;  // START + LEN + payload + CHECKSUM
    uart.BlockingTransmit(send_buffer, total_len);
}
```

#### `UartSendCmd()` - Send command-only packet (no data)

```cpp
void UartSendCmd(uint8_t cmd) {
    UartSendFrame(cmd, nullptr, 0);
}
```

### 5. RX Polling Function (`hoopi.h`)

Polls for bytes and parses framed packets with timeout handling:

```cpp
bool UartPollReceive() {
    uint8_t byte;

    // Check for timeout - reset state if packet incomplete
    if (uart_rx_state != UART_RX_WAIT_START) {
        if (System::GetNow() - uart_rx_start_time > UART_RX_TIMEOUT_MS) {
            uart_rx_state = UART_RX_WAIT_START;
        }
    }

    // Try to read one byte at a time with short timeout
    while (uart.BlockingReceive(&byte, 1, 1) == UartHandler::Result::OK) {
        switch (uart_rx_state) {
            case UART_RX_WAIT_START:
                if (byte == UART_START_BYTE) {
                    uart_rx_state = UART_RX_WAIT_LEN;
                    uart_rx_start_time = System::GetNow();
                }
                break;

            case UART_RX_WAIT_LEN:
                if (byte >= 1 && byte <= 6) {  // Valid LEN range
                    uart_rx_len = byte;
                    uart_rx_idx = 0;
                    uart_rx_state = UART_RX_READ_PAYLOAD;
                } else {
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
                        rx_payload_len = uart_rx_len;
                        uart_rx_state = UART_RX_WAIT_START;
                        return true;  // Valid packet received
                    } else {
                        uart_rx_state = UART_RX_WAIT_START;  // Checksum mismatch
                    }
                }
                break;
        }
    }
    return false;
}
```

### 6. Updated TX Message Functions (`hoopi.h`)

All transmit functions updated to use `UartSendFrame()`:

#### `SendFWVersion()` - CMD 0x04

```cpp
void SendFWVersion() {
    uint8_t data[] = {seed_fw_version};
    UartSendFrame(0x04, data, 1);
}
```

#### `SendToggleValues()` - CMD 0x06

```cpp
void SendToggleValues() {
    uint8_t data[] = {toggleValues[0], toggleValues[1], toggleValues[2]};
    UartSendFrame(0x06, data, 3);
}
```

#### `SendKnobValues()` - CMD 0x09

```cpp
void SendKnobValues() {
    hw.ProcessAnalogControls();
    uint8_t data[7];
    for (int k = 0; k < 6; k++) {
        float val = hw.GetKnobValue(static_cast<HoopiPedal::Knob>(k));
        data[k] = static_cast<uint8_t>(val * 255.0f);
    }
    // Pack effect (bits 0-3) and toggle (bits 4-7) into meta byte
    uint8_t meta = (static_cast<uint8_t>(enabled_effect) & 0x0F);
    meta |= ((toggleValues[0] & 0x01) << 4);
    meta |= ((toggleValues[1] & 0x01) << 5);
    meta |= ((toggleValues[2] & 0x01) << 6);
    data[6] = meta;
    UartSendFrame(0x09, data, 7);
}
```

#### `SendRecordingStatus()` - CMD 0x01

```cpp
void SendRecordingStatus(bool recording) {
    uint8_t data[] = {recording ? (uint8_t)2 : (uint8_t)1};
    UartSendFrame(0x01, data, 1);
}
```

#### `EnableEffect()` - CMD 0x07

```cpp
void EnableEffect(uint8_t newId) {
    if (newId < EffectCount) {
        enabled_effect = static_cast<Effect>(newId);
    } else {
        enabled_effect = Effect::Galaxy;
        newId = 0;
    }
    uint8_t data[] = {newId};
    UartSendFrame(0x07, data, 1);
}
```

### 7. Updated RX Command Processing (`hoopi.cpp`)

Main loop updated to use `UartPollReceive()` and extract data from `rx_payload[]`:

```cpp
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
        uint8_t effectIdx = rx_payload[1];
        uint8_t paramId = rx_payload[2];
        uint8_t value = rx_payload[3];
        // ... parameter handling ...

        if (handled) {
            uint8_t ack_data[] = {effectIdx, paramId, value};
            UartSendFrame(0x08, ack_data, 3);  // Param ACK
        }
    }
    else if (cmd == 0x09)  // Request Knob Values
    {
        SendKnobValues();
    }
    else if (cmd == 0xFF)  // Select Effect
    {
        EnableEffect(rx_payload[1]);
    }
    else if (cmd == 0x10)  // Control Change (Legacy MIDI CC)
    {
        uint8_t ccNum = rx_payload[1];
        uint8_t ccVal = rx_payload[2];
        bkshepherd::BaseEffectModule* module = GetEffectModule(static_cast<uint8_t>(enabled_effect));
        if (module != nullptr) {
            module->MidiCCValueNotification(ccNum, ccVal);
        }
    }

    ackUartCommand();  // Quick LED blink after command processed
}
```

---

## Protocol Compatibility

The ESP32 already has v2 protocol implemented. Both sides now communicate using:

1. **Start byte detection** - All packets begin with `0xAA`
2. **Length validation** - LEN must be 1-6 bytes
3. **XOR checksum** - Validates packet integrity
4. **Timeout handling** - Partial packets discarded after 100ms

---

## Testing

After flashing the updated firmware:

1. ESP32 should receive valid framed packets from Daisy (firmware version, toggle values, knob values)
2. Daisy should correctly parse commands from ESP32 (effect selection, parameter changes, recording control)
3. Invalid packets (bad checksum, wrong start byte) should be silently discarded

---

## Files Modified

- `/Users/prince/Developer/hoopi/seed-redux/hoopi.h` - Protocol implementation (TX, RX, checksums)
- `/Users/prince/Developer/hoopi/seed-redux/hoopi.cpp` - Command processing updated to use framed protocol
