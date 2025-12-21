# Code Cleanup Log

## 2024-12-17: Removed Unused Daisy Petal Files

Removed 2 unused template files from the Daisy Petal board that are not used by Hoopi.

| File | Reason |
|------|--------|
| `daisy_petal.cpp` | Daisy Petal template, not used (Hoopi uses `hoopi_pedal.cpp`) |
| `daisy_petal.h` | Daisy Petal template, not used (Hoopi uses `hoopi_pedal.h`) |

---

## 2024-12-17: Removed Unused Effect Files

Removed 18 unused files (2277 lines) that were no longer referenced in the codebase.

### Files Removed

| File | Reason |
|------|--------|
| `effects/amp.h` | Old wrapper, replaced by `ampsim.h` |
| `effects/all_model_data.h` | Only used by removed `neuralseed.h` |
| `effects/all_model_data_gru9_4count.h` | Only used by removed `mars.h` |
| `effects/cloudseed_module.cpp` | Newton reverb effect, not in Effect enum |
| `effects/cloudseed_module.h` | Newton reverb effect, not in Effect enum |
| `effects/cloudseed_reverb.h` | Wrapper for cloudseed, unused |
| `effects/compressor_module.cpp` | Standalone compressor, now integrated in `input_processing.h` |
| `effects/compressor_module.h` | Standalone compressor, now integrated in `input_processing.h` |
| `effects/compressor.h` | Old wrapper for compressor module |
| `effects/delayline_2tap.h` | Only used by removed `mars.h` |
| `effects/mars.h` | Unused effect wrapper |
| `effects/neuralseed.h` | Unused neural seed wrapper |
| `effects/newton.h` | Unused Newton effect wrapper |
| `effects/overdrive_module.cpp` | Overdrive effect, not in Effect enum |
| `effects/overdrive_module.h` | Overdrive effect, not in Effect enum |
| `effects/overdrive.h` | Old wrapper for overdrive module |
| `effects/ProceduralReverb.cpp` | Unused procedural reverb |
| `effects/ProceduralReverb.h` | Unused procedural reverb |

### Verification Method

Files were identified as unused by:
1. Checking includes in `hoopi.cpp`
2. Checking `CPP_SOURCES` in `Makefile`
3. Grep search for file references across codebase
4. Build verification after removal

### Current Effect Files (Retained)

**Wrappers** (in `effects/`):
- `ampsim.h` - AmpSim + GalaxyLite
- `chorus.h` - Stereo Chorus
- `delay.h` - Delay + GalaxyLite
- `distortion.h` - Distortion + GalaxyLite
- `galaxy.h` - Galaxy Reverb
- `input_processing.h` - Compressor + Noise Gate (always-on)
- `nam.h` - NAM + GalaxyLite
- `reverbsc.h` - ReverbSc
- `tremolo.h` - Tremolo + GalaxyLite

**Modules** (compiled via Makefile):
- `amp_module.cpp/h` - Neural amp simulation
- `base_effect_module.cpp/h` - Base class for effects
- `chorus_module.cpp/h` - Chorus DSP
- `delay_module.cpp/h` - Delay DSP
- `distortion_module.cpp/h` - Distortion DSP
- `galaxy_lite.h` - Lightweight FDN reverb
- `galaxy_reverb_module.cpp/h` - Full Galaxy reverb
- `looper_module.cpp/h` - Looper (used via toggle switch)
- `nam_module.cpp/h` - NAM neural amp
- `reverb_module.cpp/h` - ReverbSc wrapper
- `tremolo_module.cpp/h` - Tremolo DSP

**Support Files**:
- `Delays/delayline_reverse.h` - Reverse delay line
- `Delays/delayline_revoct.h` - Reverse/octave delay line
- `ImpulseResponse/` - IR convolution for AmpSim
- `NeuralModels/` - GRU model weights
- `Nam/` - NAM model weights
