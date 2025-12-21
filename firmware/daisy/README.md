# Hoopi Pedal - Daisy Seed Firmware

Dual-channel guitar and vocal effects processor built on the Electrosmith Daisy Seed platform.

## Features

- **8 Built-in Effects**: Galaxy Reverb, CloudSeed Reverb, AmpSim, NAM, Distortion, Delay, Tremolo, Chorus
- **Dual-Channel Design**: Guitar (L) and Microphone (R) with independent processing
- **Neural Amp Modeling**: RTNeural-based amp simulations with multiple models
- **Input Processing Chain**: 3-band EQ, Compressor, and Noise Gate
- **UART Communication**: External control via ESP32 companion device

## Requirements

- ARM GCC Toolchain (arm-none-eabi-gcc)
- Make
- DFU-util (for programming)

### macOS

```bash
brew install armmbed/formulae/arm-none-eabi-gcc
brew install dfu-util
```

### Linux (Ubuntu/Debian)

```bash
sudo apt install gcc-arm-none-eabi
sudo apt install dfu-util
```

## Building

### First-time Setup

Build the dependencies first:

```bash
# Build libDaisy
cd dependencies/libDaisy
make -j4
cd ../..

# Build DaisySP
cd dependencies/DaisySP
make -j4
cd ../..

# Build DaisySP-LGPL
cd dependencies/DaisySP/DaisySP-LGPL
make -j4
cd ../../..
```

### Build Firmware

```bash
# Build with auto-incremented version (recommended)
./build.sh

# Or standard build
make -j4

# Clean build
make clean
```

The build produces `build/hoopi_seed.bin` ready for flashing.

## Programming

### DFU Mode (USB)

1. Hold the BOOT button on the Daisy Seed
2. Press and release RESET while holding BOOT
3. Release BOOT
4. Run:

```bash
make program-dfu
# or
./program-dfu.sh
```

### ST-Link (SWD)

```bash
make program
```

## Hardware

### Inputs
- **IN L (0)**: Guitar input
- **IN R (1)**: Microphone input

### Outputs
- **OUT L (0)**: Main left output
- **OUT R (1)**: Main right output
- **OUT 2 (2)**: Recording left (to ESP32)
- **OUT 3 (3)**: Recording right (to ESP32)

### Controls
- **6 Knobs**: Effect-specific parameters
- **3 Toggle Switches**: Mode selection
- **2 Footswitches**: Bypass and recording control
- **2 LEDs**: Status indicators

## Effects

| Effect | L Channel (Guitar) | R Channel (Mic) |
|--------|-------------------|-----------------|
| Galaxy | FDN Reverb | FDN Reverb |
| CloudSeed | Algorithmic Reverb | Algorithmic Reverb |
| AmpSim | Neural Amp Model | GalaxyLite Reverb |
| NAM | Neural Amp Model | GalaxyLite Reverb |
| Distortion | Waveshaper | GalaxyLite Reverb |
| Delay | Multi-mode Delay | GalaxyLite Reverb |
| Tremolo | Multi-waveform | GalaxyLite Reverb |
| Chorus | Stereo Chorus | Stereo Chorus |

## Input Processing Chain

Applied to guitar input only (L channel):

```
Input → EQ (optional) → Noise Gate → Compressor → Effect
```

- **3-Band EQ**: Disabled by default, controlled via UART
- **Noise Gate**: Active when toggle in position 2
- **Compressor**: Active when toggle in position 1 or 2

## UART Protocol

The pedal communicates with an ESP32 companion device at 31250 baud using a framed protocol:

```
[START: 0xAA] [LEN] [CMD] [DATA...] [CHECKSUM]
```

See `docs/uart-protocol-spec.md` for full protocol documentation.

## Project Structure

```
├── hoopi.cpp/h          # Main application
├── hoopi_pedal.cpp/h    # Hardware abstraction
├── pin_defs.h           # Pin definitions
├── effects/             # Effect implementations
│   ├── *_module.cpp/h   # Effect processing modules
│   ├── *.h              # Effect wrappers
│   ├── input_processing.h  # EQ, compressor, gate
│   ├── galaxy_lite.h    # Optimized mono reverb
│   └── NeuralModels/    # Amp model weights
├── dependencies/        # External libraries
│   ├── libDaisy/        # Daisy hardware library
│   ├── DaisySP/         # DSP library
│   ├── RTNeural/        # Neural network inference
│   └── CloudSeed/       # CloudSeed reverb
└── docs/                # Documentation
```

## License

MIT License - Copyright (c) 2025-2026 Scope Creep Labs LLC

See [LICENSE.md](LICENSE.md) for details.

## Acknowledgments

- [Electrosmith](https://www.electro-smith.com/) - Daisy Seed platform
- [RTNeural](https://github.com/jatinchowdhury18/RTNeural) - Neural network inference
- [CloudSeed](https://github.com/ValdemarOrn/CloudSeed) - Reverb algorithm
- [bkshepherd](https://github.com/bkshepherd/DaisySeedProjects) - Base effect module framework
