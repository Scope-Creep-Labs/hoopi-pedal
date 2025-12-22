# Audio Processing Flow

This document describes the audio signal path through the Hoopi pedal. The flow is consistent across all effects, with AmpSim used as an example.

## Hardware I/O

### Input Channels

| Channel | Source | Description |
|---------|--------|-------------|
| in[0] | WM8731 L | Live guitar |
| in[1] | WM8731 R | Live mic |
| in[2] | ESP32 SAI2 L | Backing track guitar |
| in[3] | ESP32 SAI2 R | Backing track mic |

### Output Channels

| Channel | Destination | Description |
|---------|-------------|-------------|
| out[0] | WM8731 L | Main output left (amp/headphones) |
| out[1] | WM8731 R | Main output right (amp/headphones) |
| out[2] | ESP32 SAI2 L | Recording output left |
| out[3] | ESP32 SAI2 R | Recording output right |

## Signal Flow Diagram

```
                         INPUTS                              OUTPUTS
                    ┌─────────────────┐                 ┌─────────────────┐
    Guitar ────────►│  in[0] (L)      │    Main L ◄────│  out[0]         │
                    │                 │                 │                 │
    Mic ───────────►│  in[1] (R)      │    Main R ◄────│  out[1]         │
                    │                 │                 │                 │
    ESP32 Backing ─►│  in[2] (L)      │    Rec L ◄─────│  out[2]         │
    (Guitar)        │                 │                 │                 │
    ESP32 Backing ─►│  in[3] (R)      │    Rec R ◄─────│  out[3]         │
    (Mic)           └────────┬────────┘                 └─────────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │  AudioCallback  │
                    └────────┬────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
              ▼                             ▼
       bypass == true              bypass == false
              │                             │
              ▼                             ▼
    ┌─────────────────┐           ┌─────────────────┐
    │  Input Chain    │           │ ProcessAnalog   │
    │  (L only)       │           │ Controls        │
    └────────┬────────┘           └────────┬────────┘
             │                             │
             ▼                             ▼
    ┌─────────────────┐           ┌─────────────────┐
    │  Pass-through   │           │  Effect Process │
    │                 │           │  (per effect)   │
    └────────┬────────┘           └────────┬────────┘
             │                             │
             └──────────────┬──────────────┘
                            │
                            ▼
                   ┌─────────────────┐
                   │ Backing Track   │
                   │ Mix (CMD 0x0C)  │
                   └────────┬────────┘
                            │
                            ▼
                   ┌─────────────────┐
                   │ Output Blend    │
                   │ (stereo/mono)   │
                   └────────┬────────┘
                            │
                            ▼
                   ┌─────────────────┐
                   │    Outputs      │
                   └─────────────────┘
```

## Per-Sample Processing (All Effects)

Each effect follows this pattern within its sample loop:

```cpp
for (size_t i = 0; i < size; i++)   // size = 48 samples (1ms @ 48kHz)
{
    // 1. Read live inputs
    float inputL = in[0][i];   // Live guitar
    float inputR = in[1][i];   // Live mic

    // 2. Input chain (L channel only)
    ProcessInputChain(inputL, inputR);

    // 3. Effect-specific processing
    //    ... (varies per effect)
    //    Produces: outputL (processed guitar), outputR (processed mic)

    // 4. Get backing track channels
    float backingL = in[2][i];  // Guitar backing from ESP32
    float backingR = in[3][i];  // Mic backing from ESP32

    // 5. Mix backing track into processed signal
    float mixedL = ApplyBackingTrackMix(outputL, backingL);
    float mixedR = backingTrackBlendMic ? ApplyBackingTrackMix(outputR, backingR) : outputR;

    // 6. Main outputs (with output blend mode)
    ApplyOutputBlend(mixedL, mixedR, out[0][i], out[1][i]);

    // 7. Recording outputs (conditional backing track inclusion)
    if (backingTrackRecordBlend) {
        out[2][i] = mixedL;   // Include backing track
        out[3][i] = mixedR;
    } else {
        out[2][i] = outputL;  // Live signal only
        out[3][i] = outputR;
    }
}
```

## Input Processing Chain

The input chain applies to the **L channel (guitar) only**. The R channel (mic) always passes through clean.

### Processing Order

```
Input L → EQ → Noise Gate → Compressor → Effect
```

### 3-Band EQ (UART-controlled, disabled by default)

Controlled via UART param_id 30-36:

| Param ID | Name | Range | Default |
|----------|------|-------|---------|
| 30 | EQ Enable | 0-1 | 0 (off) |
| 31 | Low Gain | -12 to +12 dB | 0 dB (128) |
| 32 | Mid Gain | -12 to +12 dB | 0 dB (128) |
| 33 | High Gain | -12 to +12 dB | 0 dB (128) |
| 34 | Low Freq | 50-500 Hz | ~200 Hz |
| 35 | Mid Freq | 250-4000 Hz | ~1 kHz |
| 36 | High Freq | 2000-10000 Hz | ~4 kHz |

### Dynamics (Toggle Switch 2)

| Position | Mode | Processing |
|----------|------|------------|
| Left (0) | Clean | No dynamics processing |
| Middle (1) | Comp | Compressor only |
| Right (2) | Comp+Gate | Noise Gate + Compressor |

### Noise Gate (when enabled)
- Threshold: -50dB
- Attack: 1ms
- Hold: 50ms
- Release: 100ms

### Compressor (when enabled)
- Threshold: -20dB
- Ratio: 4:1
- Attack: 10ms
- Release: 100ms
- Makeup gain: 1.5x (~3.5dB)

**Note:** The input chain is active even when the effect is bypassed. This ensures consistent dynamics control during live performance.

## Backing Track Mixing

The Daisy receives backing track audio from ESP32 via I2S (SAI2) and mixes it with the live processed signal.

### UART Command (CMD 0x0C)

```
Packet: AA 04 0C <record_blend> <blend_ratio> <blend_mic> <checksum>
```

| Byte | Value | Description |
|------|-------|-------------|
| DATA 0 | 0-1 | Record blend: include backing track in recording output |
| DATA 1 | 0-127 | Blend ratio: 0 = live only, 127 = 50% backing track |
| DATA 2 | 0-1 | Blend mic: also apply blend to mic channel |

### Mixing Function

```cpp
inline float ApplyBackingTrackMix(float live, float backing) {
    if (backingTrackBlendRatio == 0) {
        return live;  // Early exit, no mixing
    }
    // 0-127 maps to 0.0-0.5 blend ratio
    float blendRatio = backingTrackBlendRatio / 254.0f;
    return live * (1.0f - blendRatio) + backing * blendRatio;
}
```

### Channel Behavior

| Flag | L Channel (Guitar) | R Channel (Mic) |
|------|-------------------|-----------------|
| Always | `mixedL = mix(outputL, in[2])` | - |
| `blend_mic=0` | - | `mixedR = outputR` (unchanged) |
| `blend_mic=1` | - | `mixedR = mix(outputR, in[3])` |

### Recording Output Behavior

| Flag | out[2] (Rec L) | out[3] (Rec R) |
|------|----------------|----------------|
| `record_blend=0` | `outputL` (live only) | `outputR` (live only) |
| `record_blend=1` | `mixedL` (with backing) | `mixedR` (with backing) |

## Output Routing

### Output Blend Modes (CMD 0x08 param 0)

| Mode | Value | Behavior |
|------|-------|----------|
| Stereo | 0 | L and R independent |
| Mono Left | 1 | Both channels sum to L, R = 0 |
| Mono Center | 2 | Both channels sum to both outputs |
| Mono Right | 3 | Both channels sum to R, L = 0 |
| Custom | 4-255 | Variable L/R blend ratio |

### ApplyOutputBlend Function

```cpp
inline void ApplyOutputBlend(float inL, float inR, float& outL, float& outR) {
    if (outputBlendMode == 0) {
        // Stereo pass-through
        outL = inL;
        outR = inR;
    } else if (outputBlendMode == 1) {
        // Mono left
        outL = (inL + inR) * 0.5f;
        outR = 0.0f;
    } else if (outputBlendMode == 2) {
        // Mono center
        float mono = (inL + inR) * 0.5f;
        outL = mono;
        outR = mono;
    } else if (outputBlendMode == 3) {
        // Mono right
        outL = 0.0f;
        outR = (inL + inR) * 0.5f;
    } else {
        // Custom blend (4-255)
        float ratioR = (outputBlendMode - 4) / 251.0f;
        float ratioL = 1.0f - ratioR;
        float mono = inL * ratioL + inR * ratioR;
        outL = mono;
        outR = mono;
    }
}
```

## Example: AmpSim Effect

### Complete Signal Flow

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         Per-Sample Processing                                 │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  LIVE INPUTS                           BACKING INPUTS (from ESP32)           │
│  ───────────                           ───────────────────────────           │
│  in[0] (Guitar)    in[1] (Mic)         in[2] (Guitar)    in[3] (Mic)         │
│       │                 │                   │                 │              │
│       ▼                 │                   │                 │              │
│  ┌─────────┐            │                   │                 │              │
│  │ 3-Band  │◄── UART param 30              │                 │              │
│  │ EQ      │    (disabled default)         │                 │              │
│  └────┬────┘            │                   │                 │              │
│       ▼                 │                   │                 │              │
│  ┌─────────┐            │                   │                 │              │
│  │ Noise   │◄── Toggle2 >= 2               │                 │              │
│  │ Gate    │                                │                 │              │
│  └────┬────┘            │                   │                 │              │
│       ▼                 │                   │                 │              │
│  ┌─────────┐            │                   │                 │              │
│  │Compres- │◄── Toggle2 >= 1               │                 │              │
│  │sor      │                                │                 │              │
│  └────┬────┘            │                   │                 │              │
│       │                 │                   │                 │              │
│       ▼                 ▼                   │                 │              │
│  ┌─────────┐      ┌─────────┐               │                 │              │
│  │ AmpSim  │      │ Galaxy  │               │                 │              │
│  │ Neural  │      │ Lite    │               │                 │              │
│  │ Model   │      │ Reverb  │               │                 │              │
│  └────┬────┘      └────┬────┘               │                 │              │
│       │                │                    │                 │              │
│       ▼                ▼                    ▼                 ▼              │
│    ampOut          reverbOut            backingL          backingR           │
│       │                │                    │                 │              │
│       └───────┬────────┘                    │                 │              │
│               │                             │                 │              │
│               ▼                             ▼                 ▼              │
│  ┌────────────────────────────────────────────────────────────────┐          │
│  │              BACKING TRACK MIX (CMD 0x0C)                      │          │
│  │                                                                │          │
│  │  mixedL = ampOut × (1-blend) + backingL × blend               │          │
│  │  mixedR = blend_mic ? reverbOut × (1-blend) + backingR × blend │          │
│  │         : reverbOut                                            │          │
│  └────────────────────────────────────────────────────────────────┘          │
│               │                                                              │
│               ▼                                                              │
│  ┌────────────────────────────────────────────────────────────────┐          │
│  │              OUTPUT BLEND (CMD 0x08 param 0)                   │          │
│  │                                                                │          │
│  │  Stereo / Mono L / Mono Center / Mono R / Custom               │          │
│  └────────────────────────────────────────────────────────────────┘          │
│               │                                                              │
│       ┌───────┴───────┐                                                      │
│       │               │                                                      │
│       ▼               ▼                                                      │
│    out[0]          out[1]         MAIN OUTPUTS (amp/headphones)              │
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────┐          │
│  │              RECORDING OUTPUT SELECTION                        │          │
│  │                                                                │          │
│  │  record_blend=0: out[2]=ampOut, out[3]=reverbOut (live only)  │          │
│  │  record_blend=1: out[2]=mixedL, out[3]=mixedR (with backing)  │          │
│  └────────────────────────────────────────────────────────────────┘          │
│               │                                                              │
│       ┌───────┴───────┐                                                      │
│       │               │                                                      │
│       ▼               ▼                                                      │
│    out[2]          out[3]         RECORDING OUTPUTS (to ESP32)               │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### AmpSim Knob Assignments

| Knob | Parameter | Range |
|------|-----------|-------|
| 1 | Gain | 0-100% (log) |
| 2 | Level | 0-100% |
| 3 | Model | 7 neural models |
| 4 | Reverb Size | 0-100% |
| 5 | Reverb Decay | 0-100% |
| 6 | Reverb Input Gain | 1-20x |

## Effect Variants

All effects follow the same input/output pattern but differ in their core processing:

### Effects with GalaxyLite for Mic (separate processing)

| Effect | L Channel (Guitar) | R Channel (Mic) |
|--------|-------------------|-----------------|
| AmpSim | Neural amp model | GalaxyLite reverb |
| NAM | Neural amp model | GalaxyLite reverb |
| Distortion | Waveshaper distortion | GalaxyLite reverb |
| Delay | Multi-mode delay | GalaxyLite reverb |
| Tremolo | Multi-waveform tremolo | GalaxyLite reverb |

### Effects with Stereo Processing (both channels through same effect)

| Effect | L Channel (Guitar) | R Channel (Mic) |
|--------|-------------------|-----------------|
| Galaxy | FDN reverb | FDN reverb |
| CloudSeed | Algorithmic reverb | Algorithmic reverb |
| Chorus | Stereo chorus | Stereo chorus |

## Performance

### Timing

- Sample rate: 48kHz
- Block size: 48 samples (1ms)
- Latency: ~1ms (one block)

### Backing Track Overhead

| Metric | Value |
|--------|-------|
| Code size increase | +3,008 bytes (+1.04%) |
| CPU cycles/sample (blend=0) | ~6 cycles |
| CPU cycles/sample (blend>0) | ~16 cycles |
| CPU overhead | 0.07% - 0.19% |

The backing track mixing adds negligible overhead compared to effect processing (neural amp model uses ~2,000-4,000 cycles/sample).
