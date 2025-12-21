# UART Parameter Configuration

This document describes the UART protocol extension for configuring effect default parameters from the ESP32.

---

## Current UART Protocol

| Byte 0 (cmd) | Meaning | Data bytes |
|--------------|---------|------------|
| 1 | Recording status | byte1: 1=stopped, 2=started |
| 2 | Start recording (from ESP32) | - |
| 3 | Reset to bootloader | - |
| 4 | FW version | byte1: version |
| 5 | ACK bootloader | - |
| 6 | Toggle values | byte1-3: toggle states |
| 7 | Effect switched | byte1: effect ID |
| 255 | Effect select (byte1) | - (via recv_buffer[1]) |

---

## New Command: Set Effect Parameter (cmd=8)

### Request Format

```
recv_buffer[0] = 8              // Command: Set effect parameter
recv_buffer[1] = effect_index   // 0-7 (Effect enum)
recv_buffer[2] = param_id       // Parameter to set (see table below)
recv_buffer[3] = value          // New value (uint8_t, 0-255)
```

### ACK Response

```
send_buffer[0] = 8              // ACK command
send_buffer[1] = effect_index   // Echo effect index
send_buffer[2] = param_id       // Echo param ID
send_buffer[3] = value          // Echo confirmed value
```

---

## Parameter IDs

| param_id | Parameter | Description |
|----------|-----------|-------------|
| 0 | Output blend mode | How L/R channels are mixed to outputs (GLOBAL) |
| 1 | GalaxyLite Damping | HF rolloff (0-255 → 0.0-1.0) (GLOBAL) |
| 2 | GalaxyLite PreDelay | Pre-delay time (0-255 → 0.0-1.0) (GLOBAL) |
| 3 | GalaxyLite Mix | Wet/dry mix (0-255 → 0.0-1.0) (GLOBAL) |
| 4-13 | Reserved | Future global parameters |
| 14-127 | MIDI CC | Effect-specific params (uses effect_index) |

**Note**: Parameters 0-3 are **global** (effect_index is ignored). Parameters 14+ use MIDI CC routing and **require the correct effect_index**.

---

## Output Blend Mode (param_id=0)

Controls how the effect's L and R outputs are mixed to the main outputs.

**Command format** (5 bytes used):
```
recv_buffer[3] = blend_mode          // 0-255 (see table below)
recv_buffer[4] = apply_to_recording  // 0=recording stays stereo, 1=apply blend to recording too
```

| Value | Mode | Description |
|-------|------|-------------|
| 0 | Stereo | L/R stay separate (no mixing) |
| 1 | Mono Center | 50/50 blend to both outputs (default) |
| 2 | Mono Left | Blend both to L output, R silent |
| 3 | Mono Right | Blend both to R output, L silent |
| 4-255 | Mono Blend Ratio | Mono output with variable L/R mix (4=100% L, 128=50/50, 255=100% R) |

**Example**: Set mono center with blend applied to recording:
```
Send: [8, 0, 0, 1, 1]   // cmd=8, idx=0(ignored), param=0, mode=1, applyToRec=1
```

---

## GalaxyLite Parameters (param_id=1,2,3) - GLOBAL

These parameters control the GalaxyLite reverb on the R (mic) channel. They are **global** settings shared across all effects that use GalaxyLite (effects 2-6).

**Note**: Effects 0 (Galaxy), 1 (Reverb), and 7 (Chorus) do NOT use GalaxyLite on R channel, so these params have no effect on them.

**Usage**: Send any effect_index (e.g., 0) - it will be ignored for these params.

### Damping (param_id=1)

Controls high-frequency rolloff in the reverb tail.

| Value | Float | Description |
|-------|-------|-------------|
| 0 | 0.0 | No damping (bright) |
| 140 | 0.55 | Default (warm, reduced sibilance) |
| 255 | 1.0 | Maximum damping (dark) |

### PreDelay (param_id=2)

Time before reverb onset, creates separation between dry and wet signal.

| Value | Float | Description |
|-------|-------|-------------|
| 0 | 0.0 | No pre-delay |
| 128 | 0.5 | Default (~25ms, maintains clarity) |
| 255 | 1.0 | Maximum pre-delay (~50ms) |

### Mix (param_id=3)

Wet/dry blend for the reverb output.

| Value | Float | Description |
|-------|-------|-------------|
| 0 | 0.0 | Dry only (no reverb) |
| 77 | 0.3 | Default (30% wet) |
| 255 | 1.0 | Wet only (100% reverb) |

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

## Effect-Specific Parameters (param_id >= 14)

For param_id >= 14, the value is routed through the effect's MIDI CC handler. The **effect_index is required** to identify which effect to configure. Value is 0-255 (scaled to 0-127 internally for MIDI).

### AmpSim (effect_index=2)

| param_id (MIDI CC) | Parameter | Value Range | Description |
|--------------------|-----------|-------------|-------------|
| 14 | Gain | 0-255 | Input gain/drive |
| 15 | Mix | 0-255 | Wet/dry blend |
| 16 | Level | 0-255 | Output level |
| 17 | Tone | 0-255 | Tone control |
| 18 | Model | 0-255 | Amp model selection (binned) |
| 19 | IR | 0-255 | IR selection (binned) |
| 20 | Neural Model | 0-255 | Neural model selection (binned) |
| 21 | IR On | 0-255 | IR bypass (0=off, 128+=on) |

### NAM (effect_index=3)

| param_id (MIDI CC) | Parameter | Value Range | Description |
|--------------------|-----------|-------------|-------------|
| 14 | Gain | 0-255 | Input gain |
| 15 | Level | 0-255 | Output level |
| 16 | Tone Bass | 0-255 | Bass EQ (binned) |
| 17 | Tone Mid | 0-255 | Mid EQ (binned) |
| 18 | Tone Treble | 0-255 | Treble EQ (binned) |
| 19 | Model | 0-255 | NAM model selection (binned) |
| 20 | IR | 0-255 | IR selection (binned) |
| 21 | IR On | 0-255 | IR bypass (0=off, 128+=on) |

### Delay (effect_index=5)

| param_id (MIDI CC) | Parameter | Value Range | Description |
|--------------------|-----------|-------------|-------------|
| 14 | Time | 0-255 | Delay time |
| 15 | Feedback | 0-255 | Feedback amount |
| 16 | Mix | 0-255 | Wet/dry blend |
| 17 | Tone | 0-255 | Delay tone/filter |
| 18 | Sync | 0-255 | Tempo sync (binned) |

### Tremolo (effect_index=6)

| param_id (MIDI CC) | Parameter | Value Range | Description |
|--------------------|-----------|-------------|-------------|
| 14 | Rate | 0-255 | Tremolo speed |
| 15 | Depth | 0-255 | Tremolo intensity |
| 16 | Wave | 0-255 | Waveform selection (binned) |
| 17 | Stereo | 0-255 | Stereo width |

### Chorus (effect_index=7)

| param_id (MIDI CC) | Parameter | Value Range | Description |
|--------------------|-----------|-------------|-------------|
| 20 | Wet | 0-255 | Wet level |
| 21 | Delay | 0-255 | Chorus delay time |
| 22 | LFO Freq | 0-255 | LFO frequency/rate |
| 23 | LFO Depth | 0-255 | LFO modulation depth |
| 24 | Feedback | 0-255 | Feedback amount |

### Galaxy (effect_index=0)

| param_id (MIDI CC) | Parameter | Value Range | Description |
|--------------------|-----------|-------------|-------------|
| 1 | Size | 0-255 | Room size |
| 14 | Input Gain L | 0-255 | Left input gain |
| 21 | Decay | 0-255 | Reverb decay time |
| 22 | Damping | 0-255 | HF damping |
| 23 | Mix | 0-255 | Wet/dry blend |
| 24 | Input Gain R | 0-255 | Right input gain |

### Reverb (effect_index=1)

| param_id (MIDI CC) | Parameter | Value Range | Description |
|--------------------|-----------|-------------|-------------|
| 1 | Time | 0-255 | Reverb time |
| 14 | Input Gain L | 0-255 | Left input gain |
| 19 | Input Gain R | 0-255 | Right input gain |
| 21 | Damping | 0-255 | HF damping |
| 22 | Mix | 0-255 | Wet/dry blend |

---

## Example Usage

### Output Blend Mode (param_id=0)

**Mono center, recording stays stereo (default behavior):**
```
Send: [8, 0, 0, 1, 0]
```

**Mono center, recording also gets mono blend:**
```
Send: [8, 0, 0, 1, 1]
```

**Stereo output (no blending):**
```
Send: [8, 0, 0, 0, 0]
```

**Mono left only (R channel silent):**
```
Send: [8, 0, 0, 2, 0]
```

**Mono right only (L channel silent):**
```
Send: [8, 0, 0, 3, 0]
```

**75% guitar / 25% mic blend:**
```
Send: [8, 0, 0, 67, 0]   // 67 ≈ 25% into the 4-255 range
```

**25% guitar / 75% mic blend:**
```
Send: [8, 0, 0, 192, 0]  // 192 ≈ 75% into the 4-255 range
```

### GalaxyLite Reverb (param_id=1,2,3)

**Set damping to maximum (dark reverb):**
```
Send: [8, 0, 1, 255]
```

**Set damping to minimum (bright reverb):**
```
Send: [8, 0, 1, 0]
```

**Set pre-delay to zero (immediate reverb):**
```
Send: [8, 0, 2, 0]
```

**Set pre-delay to maximum (~50ms):**
```
Send: [8, 0, 2, 255]
```

**Set mix to 100% wet (full reverb):**
```
Send: [8, 0, 3, 255]
```

**Set mix to 50% wet:**
```
Send: [8, 0, 3, 128]
```

**Set mix to dry (no reverb on mic):**
```
Send: [8, 0, 3, 0]
```

**Reset GalaxyLite to defaults:**
```
Send: [8, 0, 1, 140]  // Damping = 0.55
Send: [8, 0, 2, 128]  // PreDelay = 0.5
Send: [8, 0, 3, 77]   // Mix = 0.3
```

### Effect-Specific Parameters (param_id >= 14)

**Set AmpSim gain to 50%:**
```
Send: [8, 2, 14, 128]  // cmd=8, effect=2 (AmpSim), param=14 (Gain), value=128
```

**Set AmpSim level to maximum:**
```
Send: [8, 2, 16, 255]  // cmd=8, effect=2 (AmpSim), param=16 (Level), value=255
```

**Enable IR on AmpSim:**
```
Send: [8, 2, 21, 255]  // cmd=8, effect=2 (AmpSim), param=21 (IR On), value=255
```

**Disable IR on AmpSim:**
```
Send: [8, 2, 21, 0]    // cmd=8, effect=2 (AmpSim), param=21 (IR On), value=0
```

**Set NAM gain to 75%:**
```
Send: [8, 3, 14, 192]  // cmd=8, effect=3 (NAM), param=14 (Gain), value=192
```

**Set Delay time to 50%:**
```
Send: [8, 5, 14, 128]  // cmd=8, effect=5 (Delay), param=14 (Time), value=128
```

**Set Delay feedback to 25%:**
```
Send: [8, 5, 15, 64]   // cmd=8, effect=5 (Delay), param=15 (Feedback), value=64
```

**Set Tremolo rate to fast:**
```
Send: [8, 6, 14, 200]  // cmd=8, effect=6 (Tremolo), param=14 (Rate), value=200
```

**Set Tremolo depth to maximum:**
```
Send: [8, 6, 15, 255]  // cmd=8, effect=6 (Tremolo), param=15 (Depth), value=255
```

**Set Chorus wet level to 50%:**
```
Send: [8, 7, 20, 128]  // cmd=8, effect=7 (Chorus), param=20 (Wet), value=128
```

**Set Chorus LFO rate slow:**
```
Send: [8, 7, 22, 50]   // cmd=8, effect=7 (Chorus), param=22 (LFO Freq), value=50
```

---

## Implementation Notes

1. **Global params (param_id 0-3)** stored in `hoopi.h`:
   - `outputBlendMode` (default=1, mono center)
   - `applyBlendToRecording` (default=0, recording stays stereo)
   - `galaxyLiteDamping` (default=140 ≈ 0.55)
   - `galaxyLitePreDelay` (default=128 ≈ 0.5)
   - `galaxyLiteMix` (default=77 ≈ 0.3)
2. **Effect-specific params (param_id >= 14)** use MIDI CC routing:
   - `GetEffectModule(effectIdx)` returns the target effect module
   - Value is scaled from 0-255 to 0-127 (MIDI range)
   - `MidiCCValueNotification(param_id, value)` handles parameter mapping
3. Output blend is applied to main outputs (out[0]/out[1])
4. Recording outputs (out[2]/out[3]) are stereo by default, or blended if `applyBlendToRecording=1`
5. GalaxyLite params affect effects 2-6 (AmpSim, NAM, Distortion, Delay, Tremolo)
