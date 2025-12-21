# Effect Default Parameters

This document lists the fixed/default parameters that are **not mapped to knobs** for effects 2-7.

---

## Common: GalaxyLite Reverb (R Channel)

Effects 2-6 share the same GalaxyLite reverb configuration for the mic (R) channel:

| Parameter | Value | Description |
|-----------|-------|-------------|
| Damping | 0.55 | Warm tone, reduces sibilance |
| PreDelay | 0.5 | ~25ms, maintains vocal clarity |
| Mix | 0.3 | Fixed 30% wet blend |

These are optimized for vocal recording and cannot be changed via knobs.

---

## Effect 2: AmpSim

**L Channel: Amp Neural Model**

| Parameter | Value | Description |
|-----------|-------|-------------|
| Mix | 1.0 | 100% wet (full effect) |
| Tone | 1.0 | 20kHz cutoff (no filtering) |
| IR Selection | 1 | "Marsh" cabinet IR |
| Neural Model | On | GRU model enabled |
| IR Enabled | On | Cabinet impulse response enabled |

**R Channel: GalaxyLite** - See common defaults above.

---

## Effect 3: NAM

**L Channel: NAM Neural Model**

No additional fixed parameters - all NAM params are knob-controlled.

**R Channel: GalaxyLite** - See common defaults above.

---

## Effect 4: Distortion

**L Channel: Distortion**

No additional fixed parameters - all distortion params are knob-controlled.

Note: The distortion module internally uses:
- Dynamic HP pre-filter: 140-300Hz (auto-adjusts to input)
- LP post-filter: 8kHz fixed
- Tilt-tone EQ crossover

**R Channel: GalaxyLite** - See common defaults above.

---

## Effect 5: Delay

**L Channel: Delay**

| Parameter | Value | Description |
|-----------|-------|-------------|
| Tone (LPF) | 1.0 | 20kHz cutoff (no filtering on repeats) |

The delay module has a low-pass filter in the feedback path that could darken repeats, but it's set to maximum (20kHz) so repeats stay bright.

**R Channel: GalaxyLite** - See common defaults above.

---

## Effect 6: Tremolo

**L Channel: Tremolo**

| Parameter | Value | Description |
|-----------|-------|-------------|
| Mod Oscillator Wave | Sine | Rate modulation uses sine wave |

The tremolo rate can be modulated by a secondary oscillator. The modulation oscillator's waveform is fixed to sine for smooth rate variation.

**R Channel: GalaxyLite** - See common defaults above.

---

## Effect 7: Chorus

**L+R Channels: Stereo Chorus**

No fixed parameters - all 5 chorus params are knob-controlled:
1. Wet mix
2. Delay
3. LFO Frequency
4. LFO Depth
5. Feedback

**Note**: Chorus does NOT have GalaxyLite reverb on R channel. Both L and R process through the stereo chorus effect.

---

## Summary Table

| Effect | L Channel Fixed Params | R Channel |
|--------|----------------------|-----------|
| AmpSim | Mix=100%, Tone=20kHz, IR=Marsh, Neural+IR=On | GalaxyLite (common) |
| NAM | None | GalaxyLite (common) |
| Distortion | None (internal filters only) | GalaxyLite (common) |
| Delay | Tone=20kHz | GalaxyLite (common) |
| Tremolo | Mod wave=Sine | GalaxyLite (common) |
| Chorus | None | Stereo chorus (no reverb) |

---

## Why These Defaults?

### AmpSim Fixed Parameters
- **Mix=100%**: Neural amp models sound best without dry signal blending
- **Tone=20kHz**: Let the amp model determine the tone character
- **IR=Marsh**: Classic cabinet sound, works with all amp models
- **Neural+IR=On**: Full amp+cab simulation for realistic tone

### GalaxyLite Vocal Defaults
- **Damping=0.55**: Reduces harsh high frequencies from vocals/mic
- **PreDelay=25ms**: Creates separation between dry voice and reverb
- **Mix=30%**: Audible reverb without washing out the voice
