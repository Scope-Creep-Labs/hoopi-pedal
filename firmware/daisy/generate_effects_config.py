#!/usr/bin/env python3
"""
Generate a JSON configuration file for Hoopi Seed Redux effects.
This script reflects the ACTUAL knob mappings from the wrapper files and
indicates which parameters are controlled via UART only.

UART Parameter ID Ranges:
  - Global params (0-12): Output blend, GalaxyLite, Compressor, Noise Gate
  - Reserved (13): Unused
  - Effect-specific params (14-29): Routed via MIDI CC to effect modules
    Only uart_only_params should use these IDs. Knob params are NOT settable
    via UART to avoid state mismatch with physical knob positions.
  - Global EQ params (30-36): 3-band EQ (disabled by default)
  All global params are handled directly by the UART handler, effect index ignored.

Value Mapping:
  - All UART values are 0-255 (uint8_t)
  - Each param defines min/max/unit/scale for client-side conversion
  - scale: "linear" = linear interpolation, "log" = logarithmic interpolation
  - type: "enum" = discrete named values, "binned" = discrete options from list
"""

import json
import sys
from typing import Dict, List, Any, Optional

# Global param_ids reserved for global settings (not routed to effects)
GLOBAL_PARAM_ID_MAX = 13  # param_ids 0-13 are reserved for global params


def validate_uart_param_ids(config: Dict[str, Any]) -> List[str]:
    """
    Validate that uart_only_params midi_cc values don't conflict with global param_ids.
    Returns a list of error messages (empty if no conflicts).
    """
    errors = []

    for effect in config["effects"]:
        effect_name = effect["name"]
        for param in effect.get("uart_only_params", []):
            midi_cc = param.get("midi_cc")
            if midi_cc is not None and midi_cc <= GLOBAL_PARAM_ID_MAX:
                errors.append(
                    f"Effect '{effect_name}': uart_only_param '{param['name']}' has "
                    f"midi_cc={midi_cc} which conflicts with global param_id range (0-{GLOBAL_PARAM_ID_MAX}). "
                    f"Use midi_cc >= {GLOBAL_PARAM_ID_MAX + 1}."
                )

    return errors


def make_param(name: str, default: Any, midi_cc: Optional[int] = None,
               min_val: Optional[float] = None, max_val: Optional[float] = None,
               unit: Optional[str] = None, scale: str = "linear",
               param_type: Optional[str] = None, options: Optional[List[str]] = None,
               description: Optional[str] = None, target: Optional[str] = None,
               knob: Optional[int] = None, extra_byte: Optional[str] = None,
               values: Optional[Dict[str, str]] = None, param_id: Optional[int] = None) -> Dict[str, Any]:
    """Helper to create a parameter dict with only non-None fields."""
    param = {"name": name, "default": default}

    if param_id is not None:
        param["param_id"] = param_id
    if knob is not None:
        param["knob"] = knob
    if midi_cc is not None:
        param["midi_cc"] = midi_cc
    if target is not None:
        param["target"] = target
    if param_type is not None:
        param["type"] = param_type
    if options is not None:
        param["options"] = options
    if values is not None:
        param["values"] = values
    if min_val is not None:
        param["min"] = min_val
    if max_val is not None:
        param["max"] = max_val
    if unit is not None:
        param["unit"] = unit
    if scale != "linear" or (min_val is not None and param_type is None):
        param["scale"] = scale
    if description is not None:
        param["description"] = description
    if extra_byte is not None:
        param["extra_byte"] = extra_byte

    return param


def generate_effects_config() -> Dict[str, Any]:
    """Generate the complete effects configuration."""

    config = {
        "hardware": {
            "knobs": [
                {"id": 0, "name": "KNOB_1", "description": "Knob 1 (leftmost)"},
                {"id": 1, "name": "KNOB_2", "description": "Knob 2"},
                {"id": 2, "name": "KNOB_3", "description": "Knob 3"},
                {"id": 3, "name": "KNOB_4", "description": "Knob 4"},
                {"id": 4, "name": "KNOB_5", "description": "Knob 5"},
                {"id": 5, "name": "KNOB_6", "description": "Knob 6 (rightmost)"}
            ],
            "toggle_switch": {
                "name": "SWITCH_2",
                "description": "Input processing mode for L channel (guitar)",
                "positions": [
                    {"value": 0, "name": "LEFT", "mode": "Clean", "description": "No processing"},
                    {"value": 1, "name": "MIDDLE", "mode": "Compressor", "description": "Compressor only"},
                    {"value": 2, "name": "RIGHT", "mode": "Comp+Gate", "description": "Compressor + Noise Gate"}
                ]
            },
            "footswitches": [
                {"id": 1, "name": "FOOTSWITCH_1", "short_press": "Effect bypass toggle", "long_press": "Next effect"},
                {"id": 2, "name": "FOOTSWITCH_2", "short_press": "SD card recording start/stop", "long_press": "Looper secondary function"}
            ],
            "leds": [
                {"id": 1, "name": "LED_1", "description": "Effect status"},
                {"id": 2, "name": "LED_2", "description": "Recording status"}
            ]
        },
        "global_params": {
            "description": "Global parameters settable via UART (cmd=8, param_id 0-12, 30-36). Effect index ignored.",
            "params": [
                make_param("Output Blend Mode", 1, param_id=0, param_type="enum",
                           extra_byte="apply_to_recording (0=no, 1=yes)",
                           values={"0": "Stereo (L/R separate)", "1": "Mono Center (50/50 blend)",
                                   "2": "Mono Left (blend to L)", "3": "Mono Right (blend to R)"}),
                make_param("GalaxyLite Damping", 140, param_id=1, min_val=0, max_val=100, unit="%",
                           description="HF rolloff for mic reverb (0=bright, 100=dark)"),
                make_param("GalaxyLite PreDelay", 128, param_id=2, min_val=0, max_val=100, unit="%",
                           description="Pre-delay time for mic reverb"),
                make_param("GalaxyLite Mix", 77, param_id=3, min_val=0, max_val=100, unit="%",
                           description="Wet/dry for mic reverb (0=dry, 100=wet)"),
                make_param("Compressor Threshold", 102, param_id=4, min_val=-60, max_val=0, unit="dB",
                           description="Compression threshold. Active when toggle is Middle or Right."),
                make_param("Compressor Ratio", 40, param_id=5, min_val=1, max_val=20, unit=":1", scale="log",
                           description="Compression ratio"),
                make_param("Compressor Attack", 5, param_id=6, min_val=1, max_val=500, unit="ms", scale="log",
                           description="Compressor attack time"),
                make_param("Compressor Release", 12, param_id=7, min_val=10, max_val=2000, unit="ms", scale="log",
                           description="Compressor release time"),
                make_param("Compressor Makeup Gain", 85, param_id=8, min_val=1.0, max_val=4.0, unit="x",
                           description="Makeup gain multiplier"),
                make_param("Noise Gate Threshold", 128, param_id=9, min_val=-80, max_val=-20, unit="dB",
                           description="Gate threshold. Active when toggle is Right."),
                make_param("Noise Gate Attack", 5, param_id=10, min_val=0.1, max_val=50, unit="ms", scale="log",
                           description="Gate attack time"),
                make_param("Noise Gate Hold", 26, param_id=11, min_val=0, max_val=500, unit="ms", scale="log",
                           description="Gate hold time"),
                make_param("Noise Gate Release", 12, param_id=12, min_val=10, max_val=2000, unit="ms", scale="log",
                           description="Gate release time"),
                # 3-Band EQ (param_id 30-36) - disabled by default
                make_param("EQ Enable", 0, param_id=30, param_type="enum",
                           values={"0": "Off", "1": "On"},
                           description="Enable 3-band EQ (applied before dynamics processing)"),
                make_param("EQ Low Gain", 128, param_id=31, min_val=-12, max_val=12, unit="dB",
                           description="Low band gain (128=0dB)"),
                make_param("EQ Mid Gain", 128, param_id=32, min_val=-12, max_val=12, unit="dB",
                           description="Mid band gain (128=0dB)"),
                make_param("EQ High Gain", 128, param_id=33, min_val=-12, max_val=12, unit="dB",
                           description="High band gain (128=0dB)"),
                make_param("EQ Low Freq", 85, param_id=34, min_val=50, max_val=500, unit="Hz",
                           description="Low band frequency (default ~200Hz)"),
                make_param("EQ Mid Freq", 51, param_id=35, min_val=250, max_val=4000, unit="Hz", scale="log",
                           description="Mid band frequency (default ~1kHz)"),
                make_param("EQ High Freq", 64, param_id=36, min_val=2000, max_val=10000, unit="Hz", scale="log",
                           description="High band frequency (default ~4kHz)")
            ]
        },
        "effects": []
    }

    # Effect 0: Galaxy Reverb (full stereo reverb, all 6 knobs used)
    galaxy = {
        "id": 0,
        "name": "Galaxy",
        "description": "FDN-based algorithmic reverb - full stereo, all knobs mapped",
        "has_galaxylite": False,
        "knob_params": [
            make_param("Size", 0.4, knob=0, midi_cc=1, min_val=0, max_val=100, unit="%"),
            make_param("Decay", 0.5, knob=1, midi_cc=21, min_val=0, max_val=100, unit="%"),
            make_param("In Gain L", 0.0, knob=2, midi_cc=14, min_val=1, max_val=20, unit="x", scale="log"),
            make_param("Damping", 0.5, knob=3, midi_cc=22, min_val=0, max_val=100, unit="%"),
            make_param("Mix", 0.5, knob=4, midi_cc=23, min_val=0, max_val=100, unit="%"),
            make_param("In Gain R", 0.0, knob=5, midi_cc=24, min_val=1, max_val=20, unit="x", scale="log")
        ],
        "uart_only_params": []
    }

    # Effect 1: CloudSeed Reverb (full stereo reverb with presets)
    reverb = {
        "id": 1,
        "name": "CloudSeed",
        "description": "CloudSeed algorithmic reverb with 8 presets - full stereo",
        "has_galaxylite": False,
        "knob_params": [
            make_param("PreDelay", 0.0, knob=0, midi_cc=14, min_val=0, max_val=100, unit="%"),
            make_param("Decay", 0.5, knob=1, midi_cc=16, min_val=0, max_val=100, unit="%"),
            make_param("In Gain L", 0.0, knob=2, midi_cc=21, min_val=1, max_val=20, unit="x", scale="log"),
            make_param("Tone", 0.5, knob=3, midi_cc=19, min_val=0, max_val=100, unit="%"),
            make_param("Mix", 0.5, knob=4, midi_cc=15, min_val=0, max_val=100, unit="%"),
            make_param("In Gain R", 0.0, knob=5, midi_cc=22, min_val=1, max_val=20, unit="x", scale="log")
        ],
        "uart_only_params": [
            make_param("Mod Amt", 0.5, midi_cc=17, min_val=0, max_val=100, unit="%", description="Modulation amount"),
            make_param("Mod Rate", 0.5, midi_cc=18, min_val=0, max_val=100, unit="%", description="Modulation rate"),
            make_param("Preset", 6, midi_cc=20, param_type="binned",
                       options=["FChorus", "DullEchos", "Hyperplane", "MedSpace", "Hallway", "RubiKa", "SmallRoom", "90s"],
                       description="Reverb preset (1-8)")
        ]
    }

    # Effect 2: AmpSim (L: neural amp, R: GalaxyLite reverb)
    ampsim = {
        "id": 2,
        "name": "AmpSim",
        "description": "L: Neural amp model, R: GalaxyLite reverb for mic",
        "has_galaxylite": True,
        "knob_params": [
            make_param("Gain", 0.5, knob=0, midi_cc=14, target="amp", min_val=0, max_val=100, unit="%"),
            make_param("Level", 0.5, knob=1, midi_cc=16, target="amp", min_val=0, max_val=100, unit="%"),
            make_param("Model", 0, knob=2, midi_cc=18, target="amp", param_type="binned",
                       options=["Fender57", "Matchless", "Klon", "Mesa iic", "Bassman", "5150", "Splawn"]),
            make_param("Reverb Size", 0.5, knob=3, target="galaxylite", min_val=0, max_val=100, unit="%"),
            make_param("Reverb Decay", 0.5, knob=4, target="galaxylite", min_val=0, max_val=100, unit="%"),
            make_param("Reverb In Gain", 0.0, knob=5, target="galaxylite", min_val=1, max_val=20, unit="x", scale="log")
        ],
        "uart_only_params": [
            make_param("Mix", 1.0, midi_cc=15, min_val=0, max_val=100, unit="%", description="Wet/dry mix"),
            make_param("Tone", 1.0, midi_cc=17, min_val=200, max_val=20000, unit="Hz", scale="log", description="LP filter cutoff"),
            make_param("IR", 0, midi_cc=19, param_type="binned", options=["Marsh", "Proteus", "US Deluxe", "British"])
        ]
    }

    # Effect 3: NAM (L: neural amp, R: GalaxyLite reverb)
    nam = {
        "id": 3,
        "name": "NAM",
        "description": "L: Neural Amp Modeler, R: GalaxyLite reverb for mic",
        "has_galaxylite": True,
        "knob_params": [
            make_param("Gain", 0.5, knob=0, midi_cc=14, target="nam", min_val=0, max_val=100, unit="%"),
            make_param("Level", 0.5, knob=1, midi_cc=15, target="nam", min_val=0, max_val=100, unit="%"),
            make_param("Model", 0, knob=2, midi_cc=16, target="nam", param_type="binned",
                       options=["Mesa", "Match30", "DumHighG", "DumLowG", "Ethos", "Splawn", "PRSArch", "JCM800", "SansAmp", "BE-100"]),
            make_param("Reverb Size", 0.5, knob=3, target="galaxylite", min_val=0, max_val=100, unit="%"),
            make_param("Reverb Decay", 0.5, knob=4, target="galaxylite", min_val=0, max_val=100, unit="%"),
            make_param("Reverb In Gain", 0.0, knob=5, target="galaxylite", min_val=1, max_val=20, unit="x", scale="log")
        ],
        "uart_only_params": [
            make_param("Bass", 0.0, midi_cc=17, min_val=-10, max_val=10, unit="dB", description="Bass EQ (110Hz)"),
            make_param("Mid", 0.0, midi_cc=18, min_val=-10, max_val=10, unit="dB", description="Mid EQ (900Hz)"),
            make_param("Treble", 0.0, midi_cc=19, min_val=-10, max_val=10, unit="dB", description="Treble EQ (4kHz)")
        ]
    }

    # Effect 4: Distortion (L: distortion, R: GalaxyLite reverb)
    distortion = {
        "id": 4,
        "name": "Distortion",
        "description": "L: Multi-mode distortion, R: GalaxyLite reverb for mic",
        "has_galaxylite": True,
        "knob_params": [
            make_param("Gain", 0.5, knob=0, target="distortion", min_val=0, max_val=100, unit="%"),
            make_param("Level", 0.5, knob=1, target="distortion", min_val=0, max_val=100, unit="%"),
            make_param("Type", 0, knob=2, target="distortion", param_type="binned",
                       options=["Hard Clip", "Soft Clip", "Fuzz", "Tube", "Multi Stage", "Diode Clip"]),
            make_param("Reverb Size", 0.5, knob=3, target="galaxylite", min_val=0, max_val=100, unit="%"),
            make_param("Reverb Decay", 0.5, knob=4, target="galaxylite", min_val=0, max_val=100, unit="%"),
            make_param("Reverb In Gain", 0.0, knob=5, target="galaxylite", min_val=1, max_val=20, unit="x", scale="log")
        ],
        "uart_only_params": [
            make_param("Tone", 0.5, min_val=500, max_val=2000, unit="Hz", scale="log", description="Tilt tone (500Hz-2kHz)"),
            make_param("Intensity", 0.5, min_val=0, max_val=100, unit="%", description="Clipping intensity")
        ]
    }

    # Effect 5: Delay (L: delay, R: GalaxyLite reverb)
    delay = {
        "id": 5,
        "name": "Delay",
        "description": "L: Multi-mode delay, R: GalaxyLite reverb for mic",
        "has_galaxylite": True,
        "knob_params": [
            make_param("Time", 0.5, knob=0, midi_cc=14, target="delay", min_val=20, max_val=2000, unit="ms", scale="log"),
            make_param("Feedback", 0.5, knob=1, midi_cc=15, target="delay", min_val=0, max_val=100, unit="%"),
            make_param("Mix", 0.5, knob=2, midi_cc=16, target="delay", min_val=0, max_val=100, unit="%"),
            make_param("Type", 0, knob=3, midi_cc=17, target="delay", param_type="binned",
                       options=["Forward", "Reverse", "Octave", "RevOct"]),
            make_param("Reverb Size", 0.5, knob=4, target="galaxylite", min_val=0, max_val=100, unit="%"),
            make_param("Reverb Decay", 0.5, knob=5, target="galaxylite", min_val=0, max_val=100, unit="%")
        ],
        "uart_only_params": [
            make_param("Tone", 1.0, midi_cc=18, min_val=300, max_val=20000, unit="Hz", scale="log", description="Delay tone filter")
        ]
    }

    # Effect 6: Tremolo (L: tremolo, R: GalaxyLite reverb)
    tremolo = {
        "id": 6,
        "name": "Tremolo",
        "description": "L: Multi-waveform tremolo, R: GalaxyLite reverb for mic",
        "has_galaxylite": True,
        "knob_params": [
            make_param("Rate", 0.3, knob=0, midi_cc=14, target="tremolo", min_val=0.5, max_val=15, unit="Hz", scale="log"),
            make_param("Depth", 0.5, knob=1, midi_cc=15, target="tremolo", min_val=0, max_val=100, unit="%"),
            make_param("Wave", 0, knob=2, midi_cc=16, target="tremolo", param_type="binned",
                       options=["Sine", "Triangle", "Saw", "Ramp", "Square"]),
            make_param("Mod Rate", 0.0, knob=3, midi_cc=17, target="tremolo", min_val=0, max_val=100, unit="%"),
            make_param("Reverb Size", 0.5, knob=4, target="galaxylite", min_val=0, max_val=100, unit="%"),
            make_param("Reverb Decay", 0.5, knob=5, target="galaxylite", min_val=0, max_val=100, unit="%")
        ],
        "uart_only_params": []
    }

    # Effect 7: Chorus (stereo chorus, no GalaxyLite)
    chorus = {
        "id": 7,
        "name": "Chorus",
        "description": "Stereo chorus effect - all knobs mapped to chorus params",
        "has_galaxylite": False,
        "knob_params": [
            make_param("Wet", 0.65, knob=0, midi_cc=20, min_val=0, max_val=100, unit="%"),
            make_param("Delay", 0.5, knob=1, midi_cc=21, min_val=1, max_val=50, unit="ms", scale="log"),
            make_param("LFO Freq", 0.25, knob=2, midi_cc=22, min_val=0.1, max_val=10, unit="Hz", scale="log"),
            make_param("LFO Depth", 0.3, knob=3, midi_cc=23, min_val=0, max_val=100, unit="%"),
            make_param("Feedback", 0.25, knob=4, midi_cc=24, min_val=0, max_val=100, unit="%"),
            {"knob": 5, "name": "(unused)", "default": None}
        ],
        "uart_only_params": []
    }

    config["effects"] = [galaxy, reverb, ampsim, nam, distortion, delay, tremolo, chorus]

    return config


def main():
    """Main function to generate and save the effects configuration."""
    config = generate_effects_config()

    # Validate uart_only_params don't conflict with global param_ids
    errors = validate_uart_param_ids(config)
    if errors:
        print("ERROR: UART param_id conflicts detected!\n")
        for error in errors:
            print(f"  - {error}")
        print(f"\nGlobal param_ids 0-{GLOBAL_PARAM_ID_MAX} are reserved. "
              f"Effect uart_only_params must use midi_cc >= {GLOBAL_PARAM_ID_MAX + 1}.")
        sys.exit(1)

    output_file = "effects_config.json"

    with open(output_file, 'w') as f:
        json.dump(config, f, indent=2)

    print(f"Effects configuration generated successfully!")
    print(f"Output file: {output_file}")
    print(f"\nSummary:")
    print(f"  - Hardware: {len(config['hardware']['knobs'])} knobs, 1 toggle, 2 footswitches, 2 LEDs")
    print(f"  - Global UART params: {len(config['global_params']['params'])}")
    print(f"  - Effects: {len(config['effects'])}")
    for effect in config['effects']:
        knob_count = len([p for p in effect['knob_params'] if p.get('default') is not None])
        uart_count = len(effect['uart_only_params'])
        galaxylite = " + GalaxyLite R" if effect['has_galaxylite'] else ""
        print(f"    * {effect['id']}: {effect['name']} - {knob_count} knobs, {uart_count} UART-only{galaxylite}")


if __name__ == "__main__":
    main()
