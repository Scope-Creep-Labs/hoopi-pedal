# Reverb Effects Comparison

This document compares Galaxy (full) and GalaxyLite reverb implementations.

---

## Galaxy (Full Reverb)

**Files**: `effects/galaxy.h`, `effects/galaxy_reverb_module.cpp/h`

A full-featured stereo FDN reverb designed as a standalone effect.

### Architecture
- **8 channels** FDN (Feedback Delay Network)
- **4 diffusion steps** for dense, smooth reflections
- **True stereo** processing (independent L/R paths)
- Hadamard + Householder matrix mixing

### Buffer Sizes
| Buffer | Max Samples | Max Time @ 48kHz |
|--------|-------------|------------------|
| Pre-delay | 4800 | 100ms |
| FDN delay | 14400 | 300ms |
| Diffuser | 2400 | 50ms |

### Parameters (6 knobs)
| Parameter | Knob | Description |
|-----------|------|-------------|
| Size | 1 | Room size (20–300ms) |
| Decay | 2 | RT60 decay time (0.5–20s) |
| Input Gain L | 3 | Left channel input gain |
| Damping | 4 | High frequency rolloff |
| Mix | 5 | Dry/wet balance |
| Input Gain R | 6 | Right channel input gain |

### Use Case
Standalone reverb effect with full control over both L/R channels. Good for:
- Full stereo reverb processing
- When you need independent L/R gain control
- Maximum reverb quality/density

---

## GalaxyLite (Optimized Mono Reverb)

**File**: `effects/galaxy_lite.h`

A CPU-optimized mono reverb designed to pair with other effects (Amp, NAM, Distortion).

### Architecture
- **4 channels** FDN (half of Galaxy)
- **2 diffusion steps** (half of Galaxy)
- **Mono** processing (single input/output)
- Hadamard + Householder matrix mixing
- Parameter smoothing to prevent clicks
- Denormal prevention for CPU efficiency

### Buffer Sizes
| Buffer | Max Samples | Max Time @ 48kHz |
|--------|-------------|------------------|
| Pre-delay | 2400 | 50ms |
| FDN delay | 9600 | 200ms |
| Diffuser | 1200 | 25ms |

### Parameters (exposed via API)
| Parameter | Range | Description |
|-----------|-------|-------------|
| Size | 0–1 | Room size (30–180ms) |
| Decay | 0–1 | RT60 decay time (0.3–10s) |
| Damping | 0–1 | HF rolloff (1–12kHz) |
| PreDelay | 0–1 | Pre-delay (0–50ms) |
| Mix | 0–1 | Dry/wet balance |

### Fixed Defaults (when paired with effects)
When used with Amp/NAM/Distortion, these are pre-set:
- **Damping**: 0.55 (warm, reduced sibilance)
- **PreDelay**: 0.5 (~25ms, maintains clarity)
- **Mix**: 0.3 (30% wet, good for vocals)

### Use Case
Lightweight reverb for the R channel when L channel runs a CPU-intensive effect:
- Paired with Amp (GRU neural net)
- Paired with NAM (WaveNet neural net)
- Paired with Distortion

---

## Comparison Table

| Feature | Galaxy | GalaxyLite |
|---------|--------|------------|
| **FDN Channels** | 8 | 4 |
| **Diffusion Steps** | 4 | 2 |
| **Processing** | Stereo | Mono |
| **Max FDN Delay** | 300ms | 200ms |
| **Max Pre-delay** | 100ms | 50ms |
| **Max Diffuser** | 50ms | 25ms |
| **Knob Controls** | 6 | 3 (Size, Decay, Gain) |
| **CPU Usage** | Higher | Lower (~50% of Galaxy) |
| **Reverb Density** | Denser | Good |
| **Use Case** | Standalone | Paired with effects |

---

## Memory Usage (SDRAM)

### Galaxy
```
Pre-delay:  4800 × 4 bytes = 19.2 KB
FDN:        14400 × 8 channels × 4 bytes = 460.8 KB
Diffuser:   2400 × 4 steps × 8 channels × 4 bytes = 307.2 KB
Total: ~787 KB
```

### GalaxyLite
```
Pre-delay:  2400 × 4 bytes = 9.6 KB
FDN:        9600 × 4 channels × 4 bytes = 153.6 KB
Diffuser:   1200 × 2 steps × 4 channels × 4 bytes = 38.4 KB
Total: ~202 KB per instance
```

Three GalaxyLite instances (Amp, NAM, Distortion) = ~606 KB

---

## Signal Flow

### Galaxy (Stereo)
```
L Input ─┬─► Pre-delay ─► Diffusion (4 steps) ─► 8-ch FDN ─┬─► L Output
         │                                                  │
R Input ─┘                   ↑ Feedback + Damping ↓        └─► R Output
```

### GalaxyLite (Mono)
```
Input ─► Pre-delay ─► Diffusion (2 steps) ─► 4-ch FDN ─► Output
                         ↑ Feedback + Damping ↓
```

---

## When to Use Which

| Scenario | Recommendation |
|----------|----------------|
| Standalone reverb effect | Galaxy |
| Paired with neural amp (Amp/NAM) | GalaxyLite |
| Paired with Distortion | GalaxyLite |
| Maximum reverb quality | Galaxy |
| CPU-constrained setup | GalaxyLite |
| Need stereo input control | Galaxy |
| Vocal reverb on R channel | GalaxyLite |
