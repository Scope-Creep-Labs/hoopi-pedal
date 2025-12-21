# Guitar Effects Comparison

This document compares the four gain-based guitar effects in the Hoopi Seed Redux pedal.

---

## Overdrive

**File**: `effects/overdrive_module.cpp`

A simple, subtle drive effect using DaisySP's built-in Overdrive class.

### Parameters (2)
| Parameter | Knob | Range |
|-----------|------|-------|
| Drive | 1 | 0.4–0.8 |
| Level | 0 | 0.01–2.0 |

### Character
- Tube-like warmth
- Conservative clipping
- Stereo processing

---

## Distortion + GalaxyLite

**File**: `effects/distortion.h`, `effects/distortion_module.cpp`

A full-featured distortion module with multiple clipping algorithms, paired with GalaxyLite reverb on the right channel.

### Routing
- **Left channel (L input)**: Distortion processing
- **Right channel (R input)**: GalaxyLite reverb
- Toggle switch 2 controls mono/stereo blending (same as NAM)

### Parameters (6)
| Parameter | Knob | Description |
|-----------|------|-------------|
| Gain | 1 | Distortion input gain |
| Level | 2 | Distortion output volume |
| Type | 3 | Clipping algorithm (6 types) |
| Reverb Size | 4 | GalaxyLite room size |
| Reverb Decay | 5 | GalaxyLite decay time |
| Reverb Gain | 6 | GalaxyLite input gain (1-20x) |

### Clipping Types
1. **Hard Clip** - Abrupt threshold cutoff
2. **Soft Clip** - `tanh()` smooth saturation
3. **Fuzz** - Extreme compression + asymmetry + harmonic content
4. **Tube** - `atan()` saturation
5. **Multi Stage** - Cascaded: soft clip → soft clip → tube
6. **Diode Clip** - Exponential asymmetric clipping

### Distortion DSP Features
- Dynamic high-pass pre-filter (140–300Hz, adapts to input level)
- 8kHz low-pass post-filter
- Tilt-tone EQ (crossfade between LP/HP)
- Per-type volume normalization

### Signal Chain
```
L Input → Distortion → L Output
R Input → GalaxyLite Reverb → R Output
```

---

## Amp + GalaxyLite (GRU-based)

**File**: `effects/ampsim.h`, `effects/amp_module.cpp`

Neural amp simulation using GRU (Gated Recurrent Unit) networks with built-in cabinet IRs, paired with GalaxyLite reverb on the right channel.

### Routing
- **Left channel (L input)**: Amp neural processing with IR
- **Right channel (R input)**: GalaxyLite reverb
- Toggle switch 2 controls mono/stereo blending

### Neural Architecture
```
GRULayerT<float, 1, 9> → DenseT<float, 9, 1>
```
- 9-unit GRU layer with skip connection

### Parameters (6)
| Parameter | Knob | Description |
|-----------|------|-------------|
| Gain | 1 | Input gain (log curve) |
| Level | 2 | Output volume |
| Model | 3 | Amp model selection (7 models) |
| Reverb Size | 4 | GalaxyLite room size |
| Reverb Decay | 5 | GalaxyLite decay time |
| Reverb Gain | 6 | GalaxyLite input gain (1-20x) |

### Amp Models (7)
- Fender57
- Matchless
- Klon
- Mesa iic
- Bassman
- 5150
- Splawn

### Fixed Defaults (not exposed)
- Mix: 100% wet
- Tone: 20kHz (no filtering)
- IR: Marsh (first cabinet)
- Neural model & IR: Enabled

### Signal Chain
```
L Input → Gain → Neural Model → IR → Level → L Output
R Input → GalaxyLite Reverb → R Output
```

---

## NAM + GalaxyLite (WaveNet-based)

**File**: `effects/nam.h`, `effects/nam_module.cpp`

Neural amp simulation using WaveNet architecture (Neural Amp Modeler format), paired with GalaxyLite reverb on the right channel.

### Routing
- **Left channel (L input)**: NAM neural amp processing
- **Right channel (R input)**: GalaxyLite reverb
- Toggle switch 2 controls mono/stereo blending

### Neural Architecture
Uses NAM "Pico" architecture (unofficial smaller format):
```
Dilations: 1,2,4,8,16,32,64
Dilations2: 128,256,512,1,2,4,8,16,32,64,128,256,512
```
- Dilated convolutional neural network

### Parameters (6)
| Parameter | Knob | Description |
|-----------|------|-------------|
| Gain | 1 | NAM input gain (log curve) |
| Level | 2 | NAM output volume |
| Model | 3 | Amp model selection (10 models) |
| Reverb Size | 4 | GalaxyLite room size |
| Reverb Decay | 5 | GalaxyLite decay time |
| Reverb Gain | 6 | GalaxyLite input gain (1-20x) |

### Amp Models (10)
- Mesa
- Match30
- DumHighG
- DumLowG
- Ethos
- Splawn
- PRSArch
- JCM800
- SansAmp
- BE-100

### Signal Chain
```
L Input → Gain → Neural Model → Level → L Output
R Input → GalaxyLite Reverb → R Output
```

---

## Comparison Table

| Feature | Overdrive | Distortion | Amp | NAM |
|---------|-----------|------------|-----|-----|
| **Algorithm** | DaisySP Overdrive | Custom clipping | GRU neural net | WaveNet neural net |
| **Parameters** | 2 | 6 | 6 | 6 |
| **Clipping Types** | 1 | 6 | N/A | N/A |
| **Amp Models** | N/A | N/A | 7 | 10 |
| **Cabinet IR** | No | No | Yes (fixed: Marsh) | No |
| **GalaxyLite Reverb** | No | Yes (R channel) | Yes (R channel) | Yes (R channel) |
| **Stereo Routing** | L+R same | L=Dist, R=Reverb | L=Amp, R=Reverb | L=NAM, R=Reverb |
| **CPU Usage** | Low | Medium | High | Highest |

---

## Usage Recommendations

- **Overdrive**: Subtle boost, edge-of-breakup tones, stacking with other effects
- **Distortion**: High-gain sounds with vocal reverb on R channel for dual-input recording
- **Amp**: Full amp-in-a-box with IR + vocal reverb on R channel for dual-input recording
- **NAM**: Amp modeling with vocal reverb on R channel for dual-input recording
