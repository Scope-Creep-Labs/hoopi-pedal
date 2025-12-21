# Daisy Seed UART Protocol

This document describes the UART protocol for communication between ESP32 and Daisy Seed.

---

## Connection

- **Baud rate**: 31250 (MIDI standard, libDaisy default)
- **Data bits**: 8
- **Stop bits**: 1
- **Parity**: None
- **Buffer size**: 8 bytes (both TX and RX)

---

## Command Reference

### Commands FROM Daisy (RX)

| Byte 0 | Meaning | Data bytes |
|--------|---------|------------|
| 1 | Recording status | byte1: 1=stopped, 2=started |
| 4 | FW version | byte1: version (0-255) |
| 5 | ACK bootloader | - (Daisy entering DFU mode) |
| 6 | Toggle values | byte1-3: toggle switch states (0/1/2 each) |
| 7 | Effect switched | byte1: effect ID (0-7) |
| 8 | Param ACK | byte1: effect_idx, byte2: param_id, byte3: value |
| 9 | Knob values | byte1-6: knob values (0-255), byte7: effect (bits 0-3) + toggle (bits 4-7) |

### Commands TO Daisy (TX)

| Byte 0 | Meaning | Data bytes |
|--------|---------|------------|
| 1 | Stop recording | - |
| 2 | Start recording | - |
| 3 | Reset to bootloader | - (Daisy will ACK with cmd=5) |
| 8 | Set parameter | byte1: effect_idx, byte2: param_id, byte3: value, [byte4: extra] |
| 9 | Request knob values | - (Daisy responds with cmd=9) |
| 255 | Select effect | byte1: effect ID (0-7) |

---

## Effect Index Reference

| Index | Effect |
|-------|--------|
| 0 | Galaxy |
| 1 | Reverb |
| 2 | AmpSim |
| 3 | NAM |
| 4 | Distortion |
| 5 | Delay |
| 6 | Tremolo |
| 7 | Chorus |

---

## Set Parameter Command (cmd=8)

### Request Format

```
TX[0] = 8              // Command
TX[1] = effect_index   // 0-7 (required for param_id >= 14)
TX[2] = param_id       // Parameter to set
TX[3] = value          // 0-255
TX[4] = extra          // Optional (used for blend mode)
```

### Response Format

```
RX[0] = 8              // ACK
RX[1] = effect_index   // Echo
RX[2] = param_id       // Echo
RX[3] = value          // Confirmed value
```

---

## Parameter IDs

### Global Parameters (param_id 0-12)

Effect index is ignored for these - can send any value.

#### Output and Reverb (param_id 0-3)

| param_id | Parameter | Value | Description |
|----------|-----------|-------|-------------|
| 0 | Output Blend Mode | 0-255 | See blend modes below |
| 1 | GalaxyLite Damping | 0-255 | HF rolloff (0=bright, 255=dark) |
| 2 | GalaxyLite PreDelay | 0-255 | Pre-delay time |
| 3 | GalaxyLite Mix | 0-255 | Wet/dry (0=dry, 255=wet) |

#### Compressor Parameters (param_id 4-8)

Applied to L channel (guitar) input when toggle switch is in Middle or Right position.

| param_id | Parameter | Default | Range | Description |
|----------|-----------|---------|-------|-------------|
| 4 | Threshold | 102 | 0-255 → -60dB to 0dB | Compression threshold (default -20dB) |
| 5 | Ratio | 40 | 0-255 → 1:1 to 20:1 | Compression ratio (default 4:1) |
| 6 | Attack | 5 | 0-255 → 1ms to 500ms | Attack time (default 10ms) |
| 7 | Release | 12 | 0-255 → 10ms to 2000ms | Release time (default 100ms) |
| 8 | Makeup Gain | 85 | 0-255 → 1.0x to 4.0x | Makeup gain (default 1.5x) |

#### Noise Gate Parameters (param_id 9-12)

Applied to L channel (guitar) input when toggle switch is in Right position.

| param_id | Parameter | Default | Range | Description |
|----------|-----------|---------|-------|-------------|
| 9 | Threshold | 128 | 0-255 → -80dB to -20dB | Gate threshold (default -50dB) |
| 10 | Attack | 5 | 0-255 → 0.1ms to 50ms | Attack time (default 1ms) |
| 11 | Hold | 26 | 0-255 → 0ms to 500ms | Hold time (default 50ms) |
| 12 | Release | 12 | 0-255 → 10ms to 2000ms | Release time (default 100ms) |

### Effect-Specific Parameters (param_id >= 14)

Effect index IS required. Uses MIDI CC routing internally.

**Note:** Only `uart_only_params` (not exposed on physical knobs) are settable via UART.
Knob-based parameters are NOT settable via UART to avoid state mismatch with physical
knob positions. See `effects_config.json` for which params are UART-settable.

---

## Output Blend Mode (param_id=0)

**Extra byte (TX[4])**: `apply_to_recording` (0=no, 1=yes)

| Value | Mode | Description |
|-------|------|-------------|
| 0 | Stereo | L/R stay separate |
| 1 | Mono Center | 50/50 blend to both outputs (DEFAULT) |
| 2 | Mono Left | Blend to L only, R silent |
| 3 | Mono Right | Blend to R only, L silent |
| 4-255 | Blend Ratio | Mono with variable L/R mix (4=100%L, 128=50/50, 255=100%R) |

---

## GalaxyLite Defaults

These affect the mic reverb on effects 2-6 (AmpSim, NAM, Distortion, Delay, Tremolo).

| Parameter | Default Value | Float Equivalent |
|-----------|---------------|------------------|
| Damping | 140 | 0.55 |
| PreDelay | 128 | 0.50 |
| Mix | 77 | 0.30 |

---

## Effect-Specific MIDI CC Parameters

### AmpSim (effect_index=2)

| param_id | Parameter | Description |
|----------|-----------|-------------|
| 14 | Gain | Input gain/drive |
| 15 | Mix | Wet/dry blend |
| 16 | Level | Output level |
| 17 | Tone | Tone control |
| 18 | Model | Amp model (binned) |
| 19 | IR | IR selection (binned) |
| 20 | Neural Model | Neural model (binned) |
| 21 | IR On | IR bypass (0=off, 128+=on) |

### NAM (effect_index=3)

| param_id | Parameter | Description |
|----------|-----------|-------------|
| 14 | Gain | Input gain |
| 15 | Level | Output level |
| 16 | Tone Bass | Bass EQ (binned) |
| 17 | Tone Mid | Mid EQ (binned) |
| 18 | Tone Treble | Treble EQ (binned) |
| 19 | Model | NAM model (binned) |
| 20 | IR | IR selection (binned) |
| 21 | IR On | IR bypass (0=off, 128+=on) |

### Delay (effect_index=5)

| param_id | Parameter | Description |
|----------|-----------|-------------|
| 14 | Time | Delay time |
| 15 | Feedback | Feedback amount |
| 16 | Mix | Wet/dry blend |
| 17 | Tone | Filter/tone |
| 18 | Sync | Tempo sync (binned) |

### Tremolo (effect_index=6)

| param_id | Parameter | Description |
|----------|-----------|-------------|
| 14 | Rate | Speed |
| 15 | Depth | Intensity |
| 16 | Wave | Waveform (binned) |
| 17 | Stereo | Stereo width |

### Chorus (effect_index=7)

| param_id | Parameter | Description |
|----------|-----------|-------------|
| 20 | Wet | Wet level |
| 21 | Delay | Chorus delay time |
| 22 | LFO Freq | LFO rate |
| 23 | LFO Depth | Modulation depth |
| 24 | Feedback | Feedback amount |

### Galaxy (effect_index=0)

| param_id | Parameter | Description |
|----------|-----------|-------------|
| 1 | Size | Room size |
| 14 | Input Gain L | Left input gain |
| 21 | Decay | Reverb decay |
| 22 | Damping | HF damping |
| 23 | Mix | Wet/dry blend |
| 24 | Input Gain R | Right input gain |

### Reverb (effect_index=1)

| param_id | Parameter | Description |
|----------|-----------|-------------|
| 1 | Time | Reverb time |
| 14 | Input Gain L | Left input gain |
| 19 | Input Gain R | Right input gain |
| 21 | Damping | HF damping |
| 22 | Mix | Wet/dry blend |

---

## Code Examples

### Set Mono Center Output (default)

```c
uint8_t tx[8] = {8, 0, 0, 1, 0, 0, 0, 0};  // param=0, value=1, applyToRec=0
uart_write_bytes(UART_NUM, tx, 8);
```

### Set Stereo Output

```c
uint8_t tx[8] = {8, 0, 0, 0, 0, 0, 0, 0};  // param=0, value=0
uart_write_bytes(UART_NUM, tx, 8);
```

### Set GalaxyLite to Dark Reverb

```c
uint8_t tx[8] = {8, 0, 1, 255, 0, 0, 0, 0};  // param=1 (damping), value=255
uart_write_bytes(UART_NUM, tx, 8);
```

### Set GalaxyLite Mix to 50%

```c
uint8_t tx[8] = {8, 0, 3, 128, 0, 0, 0, 0};  // param=3 (mix), value=128
uart_write_bytes(UART_NUM, tx, 8);
```

### Set Compressor Threshold to -30dB

```c
uint8_t tx[8] = {8, 0, 4, 128, 0, 0, 0, 0};  // param=4 (threshold), value=128 (-30dB)
uart_write_bytes(UART_NUM, tx, 8);
```

### Set Compressor Ratio to 8:1

```c
uint8_t tx[8] = {8, 0, 5, 94, 0, 0, 0, 0};  // param=5 (ratio), value=94 (~8:1)
uart_write_bytes(UART_NUM, tx, 8);
```

### Set Noise Gate Threshold to -40dB

```c
uint8_t tx[8] = {8, 0, 9, 170, 0, 0, 0, 0};  // param=9 (gate threshold), value=170 (-40dB)
uart_write_bytes(UART_NUM, tx, 8);
```

### Set Noise Gate Hold to 100ms

```c
uint8_t tx[8] = {8, 0, 11, 51, 0, 0, 0, 0};  // param=11 (gate hold), value=51 (100ms)
uart_write_bytes(UART_NUM, tx, 8);
```

### Set AmpSim Gain to 50%

```c
uint8_t tx[8] = {8, 2, 14, 128, 0, 0, 0, 0};  // effect=2, param=14 (gain), value=128
uart_write_bytes(UART_NUM, tx, 8);
```

### Enable IR on AmpSim

```c
uint8_t tx[8] = {8, 2, 21, 255, 0, 0, 0, 0};  // effect=2, param=21 (IR On), value=255
uart_write_bytes(UART_NUM, tx, 8);
```

### Set Delay Time to 75%

```c
uint8_t tx[8] = {8, 5, 14, 192, 0, 0, 0, 0};  // effect=5, param=14 (time), value=192
uart_write_bytes(UART_NUM, tx, 8);
```

### Set Tremolo Rate Fast

```c
uint8_t tx[8] = {8, 6, 14, 230, 0, 0, 0, 0};  // effect=6, param=14 (rate), value=230
uart_write_bytes(UART_NUM, tx, 8);
```

### Select Effect

```c
uint8_t tx[8] = {255, 3, 0, 0, 0, 0, 0, 0};  // Select NAM (effect 3)
uart_write_bytes(UART_NUM, tx, 8);
```

### Request Knob Values

```c
uint8_t tx[8] = {9, 0, 0, 0, 0, 0, 0, 0};
uart_write_bytes(UART_NUM, tx, 8);
// Response: rx[0]=9, rx[1-6]=knob values (0-255)
// rx[7] = effect (bits 0-3) | toggle (bits 4-7)
// Extract: effect = rx[7] & 0x0F, toggle = rx[7] >> 4
```

### Reset to Bootloader (DFU)

```c
uint8_t tx[8] = {3, 0, 0, 0, 0, 0, 0, 0};
uart_write_bytes(UART_NUM, tx, 8);
// Wait for ACK (rx[0] == 5), then Daisy enters DFU mode
```

---

## ESP32 HTTP API Endpoints

### GET /api/effects_config

Returns the embedded `effects_config.json` with all effect definitions, parameters, and defaults.

### POST /api/setparam

Set a parameter value. Parameters are persisted to NVS and restored on reboot.

**Request body:**
```json
{
  "effect_idx": 0-7,      // Required for effect-specific params (>=14)
  "param_id": 0-255,      // Parameter ID
  "value": 0-255,         // Value to set
  "extra": 0-255          // Optional (used for blend mode apply_to_recording)
}
```

**Validation:**
- Global params (0-12): Always allowed
- Effect params (14-24): Only uart_only params allowed (not knob params)

**UART-only params per effect:**
| Effect | Param IDs | Parameters |
|--------|-----------|------------|
| 0 Galaxy | - | None |
| 1 Reverb | - | None |
| 2 AmpSim | 15, 17, 19, 20, 21 | Mix, Tone, IR, NeuralModel, IR On |
| 3 NAM | 17, 18, 19, 20, 21 | Bass, Mid, Treble, NeuralModel, EQ |
| 4 Distortion | - | None (no MIDI CC) |
| 5 Delay | 18 | Tone |
| 6 Tremolo | - | None |
| 7 Chorus | - | None |

**Response:**
```json
{"status":"ok","effect_idx":2,"param_id":19,"value":128,"confirmed_value":128}
```

**Error (knob param):**
```json
{"status":"error","message":"param_id 14 is not a UART-settable parameter for effect 2 (knob params cannot be set via API)"}
```

### POST /api/selecteffect

Select the active effect.

**Request body:**
```json
{"effect_id": 0-7}
```

### GET /api/params

Get current values of all parameters (global + effect-specific uart_only params + knobs).

Requests knob values from Daisy via UART cmd=9 and waits up to 1000ms for response.

**Response:**
```json
{
  "current_effect": 2,
  "global": {
    "blend_mode": 1,
    "blend_apply_to_rec": 0,
    "galaxylite_damping": 140,
    "galaxylite_predelay": 128,
    "galaxylite_mix": 77,
    "comp_threshold": 102,
    "comp_ratio": 40,
    "comp_attack": 5,
    "comp_release": 12,
    "comp_makeup": 85,
    "gate_threshold": 128,
    "gate_attack": 5,
    "gate_hold": 26,
    "gate_release": 12
  },
  "effects": [
    {
      "id": 2,
      "name": "ampsim",
      "params": {
        "15": null,
        "17": null,
        "19": 128,
        "20": null,
        "21": 255
      }
    }
  ],
  "knobs": {
    "values": [128, 64, 200, 100, 50, 180],
    "effect": 2,
    "toggle": 1
  }
}
```

**Notes:**
- `global` contains all global params (0-12) with current values
- `effects` array only includes effects that have uart_only params
- Each param shows its value, or `null` if not set (default 255)
- `knobs.values` is array of 6 knob positions (0-255, scaled from 0.0-1.0)
- `knobs.effect` is the current effect ID from Daisy (0-7)
- `knobs.toggle` is the toggle switch state from Daisy (0-2)
- If Daisy doesn't respond, `knobs` fields will be `null`

---

## Parameter Persistence

The ESP32 persists parameters to NVS (Non-Volatile Storage) and restores them on boot.

### What is persisted:

1. **Global parameters (param_id 0-12)**
   - Output blend mode, GalaxyLite settings, compressor, noise gate

2. **Effect-specific parameters (param_id 14-24)**
   - Only uart_only params (not knob params)
   - Stored per effect: `effect_params[effect_id][param_id - 14]`

3. **Current effect selection**
   - `programId` (0-7)

### Startup sync flow:

1. `load_program_config()` - Loads all params from NVS
2. `send_sync()` - Sends to Daisy:
   - Effect selection (cmd=255)
   - All 13 global params (cmd=8, param 0-12)
   - All saved effect-specific params (cmd=8, param 14+)

### NVS Keys:

| Key | Description |
|-----|-------------|
| `programId` | Current effect (0-7) |
| `blendMode` | Output blend mode |
| `glDamping` | GalaxyLite damping |
| `glPredelay` | GalaxyLite pre-delay |
| `glMix` | GalaxyLite mix |
| `compThresh` | Compressor threshold |
| `compRatio` | Compressor ratio |
| `compAttack` | Compressor attack |
| `compRelease` | Compressor release |
| `compMakeup` | Compressor makeup gain |
| `gateThresh` | Noise gate threshold |
| `gateAttack` | Noise gate attack |
| `gateHold` | Noise gate hold |
| `gateRelease` | Noise gate release |
| `e{N}p{M}` | Effect N, param M (e.g., `e2p19` = AmpSim IR) |

---

## Notes

1. All values are 0-255 (uint8_t)
2. For effect-specific params, value is scaled internally (0-255 -> 0-127 for MIDI)
3. Daisy sends ACK for cmd=8 with confirmed values
4. GalaxyLite params only affect effects 2-6 (not Galaxy, Reverb, or Chorus)
5. Recording outputs stay stereo by default unless `apply_to_recording=1`
6. Knob params cannot be set via API - use physical knobs
7. Parameters set via API are persisted and restored on reboot
