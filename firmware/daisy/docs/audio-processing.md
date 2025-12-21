# Audio Processing Flow

This document describes the audio signal path through the Hoopi pedal. The flow is consistent across all effects, with AmpSim used as an example.

## Hardware I/O

| Channel | Input | Output |
|---------|-------|--------|
| L (0) | Guitar | Main L |
| R (1) | Mic | Main R |
| Rec L (2) | - | ESP32 L (stereo) |
| Rec R (3) | - | ESP32 R (stereo) |

## Signal Flow Diagram

```
                    ┌─────────────────┐
    Guitar ────────►│  in[0] (L)      │
                    │                 │
    Mic ───────────►│  in[1] (R)      │
                    └────────┬────────┘
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
                   │    Outputs      │
                   └─────────────────┘
```

## Per-Sample Processing (All Effects)

Each effect follows this pattern within its sample loop:

```cpp
for (size_t i = 0; i < size; i++)   // size = 48 samples (1ms @ 48kHz)
{
    // 1. Read raw inputs
    float inputL = in[0][i];   // Guitar
    float inputR = in[1][i];   // Mic

    // 2. Input chain (L channel only)
    ProcessInputChain(inputL, inputR);

    // 3. Effect-specific processing
    //    ... (varies per effect)

    // 4. Recording outputs (always stereo)
    out[2][i] = outputL;
    out[3][i] = outputR;

    // 5. Main outputs (mono mix)
    ApplyMonoMix(outputL, outputR, out[0][i], out[1][i]);
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

## Output Routing

### Main Outputs (out[0], out[1])
Both L and R main outputs receive a **mono mix**:
```cpp
float mono = (outputL + outputR) * 0.5f;
out[0][i] = mono;  // Main L
out[1][i] = mono;  // Main R
```

### Recording Outputs (out[2], out[3])
Recording outputs maintain **stereo separation** for the ESP32:
```cpp
out[2][i] = outputL;  // Processed L (guitar/effect)
out[3][i] = outputR;  // Processed R (reverb/mic)
```

## Example: AmpSim Effect

```
┌─────────────────────────────────────────────────────────────────┐
│                     Per-Sample Processing                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  in[0] (Guitar)    in[1] (Mic)                                  │
│       │                 │                                        │
│       ▼                 │                                        │
│  ┌─────────┐            │                                        │
│  │ 3-Band  │◄─── UART param 30 (disabled by default)           │
│  │ EQ      │                                                    │
│  └────┬────┘            │                                        │
│       ▼                 │                                        │
│  ┌─────────┐            │                                        │
│  │ Noise   │◄─── Toggle2 >= 2                                   │
│  │ Gate    │                                                    │
│  └────┬────┘            │                                        │
│       ▼                 │                                        │
│  ┌─────────┐            │                                        │
│  │Compres- │◄─── Toggle2 >= 1                                   │
│  │sor      │                                                    │
│  └────┬────┘            │                                        │
│       │                 │                                        │
│       ▼                 ▼                                        │
│  ┌─────────┐      ┌─────────┐                                   │
│  │ AmpSim  │      │ Galaxy  │                                   │
│  │ Neural  │      │ Lite    │                                   │
│  │ Model   │      │ Reverb  │                                   │
│  └────┬────┘      └────┬────┘                                   │
│       │                │                                        │
│       ▼                ▼                                        │
│    ampOut          reverbOut                                    │
│       │                │                                        │
│       ├────────┬───────┤                                        │
│       │        │       │                                        │
│       ▼        ▼       ▼                                        │
│   out[2]   MonoMix   out[3]                                     │
│   (RecL)      │      (RecR)                                     │
│               ▼                                                  │
│         out[0], out[1]                                          │
│         (Main L & R)                                            │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
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

## Timing

- Sample rate: 48kHz
- Block size: 48 samples (1ms)
- Latency: ~1ms (one block)
