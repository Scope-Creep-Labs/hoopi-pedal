# UART Protocol Specification v2.0

Communication protocol between ESP32 and Daisy Seed.

## Physical Layer

- **Baud rate**: 31250
- **Data bits**: 8
- **Parity**: None
- **Stop bits**: 1
- **Flow control**: None

## Packet Format

All packets use the following framed format:

```
+-------+-----+-----+----------+----------+
| START | LEN | CMD | DATA ... | CHECKSUM |
+-------+-----+-----+----------+----------+
   1B     1B    1B    0-7 bytes    1B
```

| Field    | Size    | Description                                      |
|----------|---------|--------------------------------------------------|
| START    | 1 byte  | Sync byte, always `0xAA`                         |
| LEN      | 1 byte  | Payload length (CMD + DATA bytes, 1-8)           |
| CMD      | 1 byte  | Command identifier                               |
| DATA     | 0-7 B   | Command-specific payload                         |
| CHECKSUM | 1 byte  | XOR of all bytes from LEN through last DATA byte |

**Maximum packet size**: 11 bytes (START + LEN + CMD + 7 DATA + CHECKSUM)

### Checksum Calculation

```c
uint8_t calculate_checksum(uint8_t len, uint8_t cmd, uint8_t *data, uint8_t data_len) {
    uint8_t checksum = len ^ cmd;
    for (int i = 0; i < data_len; i++) {
        checksum ^= data[i];
    }
    return checksum;
}
```

### Packet Validation

Receivers must:
1. Wait for START byte (`0xAA`)
2. Read LEN byte, validate it's between 1-6
3. Read LEN bytes (CMD + DATA)
4. Read CHECKSUM byte
5. Verify checksum matches calculated value
6. Discard packet and resync if checksum fails

---

## Commands: ESP32 → Daisy

### CMD 0x01: Stop Recording

Request Daisy to stop recording mode.

| Byte | Value | Description      |
|------|-------|------------------|
| CMD  | 0x01  | Stop recording   |

**Packet**: `AA 01 01 00`

**Response**: Recording Status (0x01)

---

### CMD 0x02: Start Recording

Request Daisy to start recording mode.

| Byte | Value | Description      |
|------|-------|------------------|
| CMD  | 0x02  | Start recording  |

**Packet**: `AA 01 02 03`

**Response**: Recording Status (0x01)

---

### CMD 0x03: Reset to Bootloader

Request Daisy to enter DFU bootloader mode.

| Byte | Value | Description         |
|------|-------|---------------------|
| CMD  | 0x03  | Reset to bootloader |

**Packet**: `AA 01 03 02`

**Response**: ACK Bootloader (0x05)

---

### CMD 0x08: Set Parameter

Set an effect parameter value.

| Byte   | Value | Description                          |
|--------|-------|--------------------------------------|
| CMD    | 0x08  | Set parameter                        |
| DATA 0 | 0-7   | Effect index (0=Galaxy, 1=Reverb...) |
| DATA 1 | 0-24  | Parameter ID                         |
| DATA 2 | 0-255 | Parameter value                      |
| DATA 3 | 0-255 | Extra byte (optional, for blend mode)|

**Packet (4 data bytes)**: `AA 05 08 <effect> <param> <value> <extra> <checksum>`

**Response**: Param ACK (0x08)

---

### CMD 0x09: Request Knob Values

Request current knob positions from Daisy.

| Byte | Value | Description        |
|------|-------|--------------------|
| CMD  | 0x09  | Request knob values|

**Packet**: `AA 01 09 08`

**Response**: Knob Values (0x09)

---

### CMD 0xFF: Select Effect

Change the active effect.

| Byte   | Value | Description               |
|--------|-------|---------------------------|
| CMD    | 0xFF  | Select effect             |
| DATA 0 | 0-7   | Effect index to activate  |

**Packet**: `AA 02 FF <effect> <checksum>`

**Response**: Effect Switched (0x07)

---

### CMD 0x10: Control Change (Legacy MIDI CC)

Send a control change value.

| Byte   | Value | Description      |
|--------|-------|------------------|
| CMD    | 0x10  | Control change   |
| DATA 0 | 0-127 | Control number   |
| DATA 1 | 0-127 | Control value    |

**Packet**: `AA 03 10 <cc_num> <cc_val> <checksum>`

**Response**: None

---

## Commands: Daisy → ESP32

### CMD 0x01: Recording Status

Sent when recording state changes.

| Byte   | Value | Description                    |
|--------|-------|--------------------------------|
| CMD    | 0x01  | Recording status               |
| DATA 0 | 1-2   | 1=stopped, 2=started           |

**Packet**: `AA 02 01 <status> <checksum>`

---

### CMD 0x03: Sync Request

Daisy requests ESP32 to resend all parameters.

| Byte | Value | Description  |
|------|-------|--------------|
| CMD  | 0x03  | Sync request |

**Packet**: `AA 01 03 02`

---

### CMD 0x04: Firmware Version

Daisy reports its firmware version.

| Byte   | Value | Description       |
|--------|-------|-------------------|
| CMD    | 0x04  | Firmware version  |
| DATA 0 | 0-255 | Version number    |

**Packet**: `AA 02 04 <version> <checksum>`

---

### CMD 0x05: ACK Bootloader

Acknowledge bootloader mode entry.

| Byte | Value | Description    |
|------|-------|----------------|
| CMD  | 0x05  | ACK bootloader |

**Packet**: `AA 01 05 04`

---

### CMD 0x06: Toggle Values

Report current toggle switch states (sent on startup).

| Byte   | Value | Description    |
|--------|-------|----------------|
| CMD    | 0x06  | Toggle values  |
| DATA 0 | 0-1   | Toggle 1 state |
| DATA 1 | 0-1   | Toggle 2 state |
| DATA 2 | 0-1   | Toggle 3 state |

**Packet**: `AA 04 06 <t1> <t2> <t3> <checksum>`

---

### CMD 0x07: Effect Switched

Confirm active effect change.

| Byte   | Value | Description         |
|--------|-------|---------------------|
| CMD    | 0x07  | Effect switched     |
| DATA 0 | 0-7   | Active effect index |

**Packet**: `AA 02 07 <effect> <checksum>`

---

### CMD 0x08: Param ACK

Acknowledge parameter set.

| Byte   | Value | Description      |
|--------|-------|------------------|
| CMD    | 0x08  | Param ACK        |
| DATA 0 | 0-7   | Effect index     |
| DATA 1 | 0-24  | Parameter ID     |
| DATA 2 | 0-255 | Confirmed value  |

**Packet**: `AA 04 08 <effect> <param> <value> <checksum>`

---

### CMD 0x09: Knob Values

Report current knob positions.

| Byte   | Value | Description                           |
|--------|-------|---------------------------------------|
| CMD    | 0x09  | Knob values                           |
| DATA 0 | 0-255 | Knob 1 value (scaled from 0.0-1.0)    |
| DATA 1 | 0-255 | Knob 2 value                          |
| DATA 2 | 0-255 | Knob 3 value                          |
| DATA 3 | 0-255 | Knob 4 value                          |
| DATA 4 | 0-255 | Knob 5 value                          |
| DATA 5 | 0-255 | Knob 6 value                          |
| DATA 6 | 0-255 | Effect (bits 0-3) + Toggle (bits 4-7) |

**Packet**: `AA 08 09 <k1> <k2> <k3> <k4> <k5> <k6> <meta> <checksum>`

---

## Effect Index Mapping

| Index | Effect      |
|-------|-------------|
| 0     | Galaxy      |
| 1     | Reverb      |
| 2     | AmpSim      |
| 3     | NAM         |
| 4     | Distortion  |
| 5     | Delay       |
| 6     | Tremolo     |
| 7     | Chorus      |

---

## Global Parameters (param_id 0-12, 30-36)

Effect index is ignored for these parameters.

| ID | Name                  | Range   | Default | Notes |
|----|-----------------------|---------|---------|-------|
| 0  | Output Blend Mode     | 0-255   | 1       | |
| 1  | GalaxyLite Damping    | 0-255   | 140     | |
| 2  | GalaxyLite Pre-delay  | 0-255   | 128     | |
| 3  | GalaxyLite Mix        | 0-255   | 77      | |
| 4  | Compressor Threshold  | 0-255   | 102     | -60dB to 0dB |
| 5  | Compressor Ratio      | 0-255   | 40      | 1:1 to 20:1 |
| 6  | Compressor Attack     | 0-255   | 5       | 1ms to 500ms |
| 7  | Compressor Release    | 0-255   | 12      | 10ms to 2000ms |
| 8  | Compressor Makeup     | 0-255   | 85      | 1.0x to 4.0x |
| 9  | Gate Threshold        | 0-255   | 128     | -80dB to -20dB |
| 10 | Gate Attack           | 0-255   | 5       | 0.1ms to 50ms |
| 11 | Gate Hold             | 0-255   | 26      | 0ms to 500ms |
| 12 | Gate Release          | 0-255   | 12      | 10ms to 2000ms |
| 30 | EQ Enable             | 0-1     | 0       | 0=off, 1=on |
| 31 | EQ Low Gain           | 0-255   | 128     | -12dB to +12dB (128=0dB) |
| 32 | EQ Mid Gain           | 0-255   | 128     | -12dB to +12dB (128=0dB) |
| 33 | EQ High Gain          | 0-255   | 128     | -12dB to +12dB (128=0dB) |
| 34 | EQ Low Freq           | 0-255   | 85      | 50Hz to 500Hz |
| 35 | EQ Mid Freq           | 0-255   | 51      | 250Hz to 4kHz |
| 36 | EQ High Freq          | 0-255   | 64      | 2kHz to 10kHz |

Note: param_id 14-29 are reserved for effect-specific parameters via MIDI CC mapping.

---

## Implementation Notes

### Synchronization

After receiving START byte (`0xAA`), if subsequent bytes fail validation (bad length or checksum), discard all bytes until the next `0xAA` is found.

### Timeout

If a complete packet is not received within 100ms of the START byte, discard partial data and reset to waiting for START.

### Backwards Compatibility

During transition, Daisy may check for START byte to detect protocol version:
- If first byte is `0xAA`: Use v2.0 framed protocol
- Otherwise: Use legacy 8-byte unframed protocol
