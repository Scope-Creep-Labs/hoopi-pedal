/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "uart_commands.h"
#include "hoopi_pedal.h"
#include "uart_protocol.h"
#include "audio_config.h"
#include "controls.h"
#include "ota.h"
#include "nam_models.h"
#include "effects/input_processing.h"
#include "effects/base_effect_module.h"

using namespace daisy;

// External hardware reference
extern HoopiPedal hw;
extern InputProcessor inputProcessor;

// Effect state
Effect enabled_effect = Effect::Galaxy;
bool bypass = true;

// Firmware version
uint8_t fw_version_major = FW_VERSION_MAJOR;
uint8_t fw_version_minor = FW_VERSION_MINOR;

// Forward declarations for effect modules
extern bkshepherd::BaseEffectModule* galaxyModule;
extern bkshepherd::BaseEffectModule* cloudSeedModule;
extern bkshepherd::BaseEffectModule* ampSimModule;
extern bkshepherd::BaseEffectModule* namModule;
extern bkshepherd::BaseEffectModule* distortionModule;
extern bkshepherd::BaseEffectModule* delayModule;
extern bkshepherd::BaseEffectModule* tremoloModule;
extern bkshepherd::BaseEffectModule* chorusModule;

bkshepherd::BaseEffectModule* GetEffectModule(uint8_t effectIdx) {
    switch (effectIdx) {
        case Effect::Galaxy:     return galaxyModule;
        case Effect::Reverb:     return cloudSeedModule;
        case Effect::AmpSim:     return ampSimModule;
        case Effect::NamSim:     return namModule;
        case Effect::Distortion: return distortionModule;
        case Effect::Delay:      return delayModule;
        case Effect::Tremolo:    return tremoloModule;
        case Effect::ChorusSim:  return chorusModule;
        default:                 return nullptr;
    }
}

void EnableEffect(uint8_t newId) {
    if (newId < EffectCount) {
        enabled_effect = static_cast<Effect>(newId);
    } else {
        enabled_effect = Effect::Galaxy;
        newId = 0;
    }
    uint8_t data[] = {newId};
    UartSendFrame(0x07, data, 1);  // Effect Switched (cmd=7)
}

void SendDeviceInfo() {
    uint8_t data[] = {0x01, fw_version_major, fw_version_minor};  // type=1 (Hoopi)
    UartSendFrame(0x0E, data, 3);  // Device Info (cmd=0x0E)
}

void SendToggleValues() {
    uint8_t data[] = {
        static_cast<uint8_t>(toggleValues[0]),
        static_cast<uint8_t>(toggleValues[1]),
        static_cast<uint8_t>(toggleValues[2])
    };
    UartSendFrame(0x06, data, 3);  // Toggle Values (cmd=6)
}

void SendKnobValues() {
    uint8_t data[8];
    for (int i = 0; i < 6; i++) {
        float val = hw.GetKnobValue(static_cast<HoopiPedal::Knob>(i));
        data[i] = static_cast<uint8_t>(val * 255.0f);
    }
    // Pack effect (lower 4 bits) and toggle switch (upper 4 bits) into byte7
    data[6] = static_cast<uint8_t>(enabled_effect) | (static_cast<uint8_t>(toggleValues[1]) << 4);
    data[7] = bypass ? 1 : 0;
    UartSendFrame(0x09, data, 8);  // Knob Values (cmd=9)
}

bool ProcessUartCommand(uint8_t cmd, uint8_t* payload, uint16_t payload_len) {
    // OTA commands (0x20-0x24) are handled separately
    if (cmd >= 0x20 && cmd <= 0x24) {
        return ProcessOtaCommand(cmd, payload, payload_len);
    }

    // NAM Model OTA commands (0x30-0x34)
    if (cmd >= CMD_NAM_PACK_START && cmd <= CMD_NAM_PACK_STATUS) {
        NamOtaProcessCommand(cmd, payload, payload_len);
        return true;
    }

    switch (cmd) {
        case 0x02:  // Start Recording
            StartRecording();
            return true;

        case 0x01:  // Stop Recording
            StopRecording();
            return true;

        case 0x03:  // Reset to Bootloader
        {
            uint8_t bootloader_type = (payload_len > 1) ? payload[1] : 0;

            UartSendCmd(0x05);  // ACK Bootloader
            System::Delay(10);  // Ensure UART TX completes

            if (bootloader_type == 0) {
                // STM32 ROM bootloader
                typedef void (*pFunction)(void);
                __disable_irq();
                HAL_RCC_DeInit();
                HAL_DeInit();
                uint32_t sys_mem = 0x1FF09800;
                __set_MSP(*(volatile uint32_t *)sys_mem);
                pFunction jump = (pFunction)(*(volatile uint32_t *)(sys_mem + 4));
                jump();
            } else if (bootloader_type == 1) {
                // Daisy bootloader
                System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
            } else if (bootloader_type == 2) {
                // OTA bootloader
                volatile uint32_t *boot_info = (volatile uint32_t *)0x38800000;
                boot_info[0] = 0xB007F1A6;  // Magic
                boot_info[1] = 0x00000001;  // BOOT_FLAG_OTA_REQUEST
                HAL_NVIC_SystemReset();
            }
            return true;
        }

        case 0x0E:  // Request Device Info
            SendDeviceInfo();
            return true;

        case 0x08:  // Set Parameter
        {
            uint8_t effectIdx = payload[1];
            uint8_t paramId = payload[2];
            uint8_t value = payload[3];

            bool handled = false;
            bool isGlobal = (effectIdx == 255 || effectIdx == 0);

            // Global params (param_id 0-12 and 30-37)
            if (isGlobal && paramId == 0) {
                outputBlendMode = value;
                applyBlendToRecording = (payload_len > 4) ? payload[4] : 0;
                handled = true;
            }
            else if (isGlobal && paramId == 1) {
                galaxyLiteDamping = value;
                handled = true;
            }
            else if (isGlobal && paramId == 2) {
                galaxyLitePreDelay = value;
                handled = true;
            }
            else if (isGlobal && paramId == 3) {
                galaxyLiteMix = value;
                handled = true;
            }
            // Compressor params (param_id 4-8)
            else if (isGlobal && paramId == 4) {
                compThreshold = value;
                inputProcessor.SetCompThreshold(value);
                handled = true;
            }
            else if (isGlobal && paramId == 5) {
                compRatio = value;
                inputProcessor.SetCompRatio(value);
                handled = true;
            }
            else if (isGlobal && paramId == 6) {
                compAttack = value;
                inputProcessor.SetCompAttack(value);
                handled = true;
            }
            else if (isGlobal && paramId == 7) {
                compRelease = value;
                inputProcessor.SetCompRelease(value);
                handled = true;
            }
            else if (isGlobal && paramId == 8) {
                compMakeupGain = value;
                inputProcessor.SetCompMakeupGain(value);
                handled = true;
            }
            // Noise gate params (param_id 9-12)
            else if (isGlobal && paramId == 9) {
                gateThreshold = value;
                inputProcessor.SetGateThreshold(value);
                handled = true;
            }
            else if (isGlobal && paramId == 10) {
                gateAttack = value;
                inputProcessor.SetGateAttack(value);
                handled = true;
            }
            else if (isGlobal && paramId == 11) {
                gateHold = value;
                inputProcessor.SetGateHold(value);
                handled = true;
            }
            else if (isGlobal && paramId == 12) {
                gateRelease = value;
                inputProcessor.SetGateRelease(value);
                handled = true;
            }
            // EQ params (param_id 30-36)
            else if (isGlobal && paramId == 30) {
                eqEnabled = value;
                inputProcessor.SetEqEnabled(value != 0);
                handled = true;
            }
            else if (isGlobal && paramId == 31) {
                eqLowGain = value;
                inputProcessor.SetEqLowGain(value);
                handled = true;
            }
            else if (isGlobal && paramId == 32) {
                eqMidGain = value;
                inputProcessor.SetEqMidGain(value);
                handled = true;
            }
            else if (isGlobal && paramId == 33) {
                eqHighGain = value;
                inputProcessor.SetEqHighGain(value);
                handled = true;
            }
            else if (isGlobal && paramId == 34) {
                eqLowFreq = value;
                inputProcessor.SetEqLowFreq(value);
                handled = true;
            }
            else if (isGlobal && paramId == 35) {
                eqMidFreq = value;
                inputProcessor.SetEqMidFreq(value);
                handled = true;
            }
            else if (isGlobal && paramId == 36) {
                eqHighFreq = value;
                inputProcessor.SetEqHighFreq(value);
                handled = true;
            }
            // Input processing master enable (param_id 37)
            else if (isGlobal && paramId == 37) {
                inputProcessingEnabled = (value != 0);
                handled = true;
            }
            // Effect-specific params via MIDI CC (param_id 14-29)
            else if (!isGlobal && paramId >= 14 && paramId <= 29) {
                bkshepherd::BaseEffectModule* module = GetEffectModule(effectIdx);
                if (module != nullptr) {
                    uint8_t midiValue = value >> 1;  // 0-255 -> 0-127
                    module->MidiCCValueNotification(paramId, midiValue);
                    handled = true;
                }
            }
            // Effect-specific params (param_id >= 38)
            else if (!isGlobal && paramId >= 38) {
                bkshepherd::BaseEffectModule* module = GetEffectModule(effectIdx);
                if (module != nullptr) {
                    uint8_t midiValue = value >> 1;
                    module->MidiCCValueNotification(paramId, midiValue);
                    handled = true;
                }
            }

            if (handled) {
                uint8_t ack_data[] = {effectIdx, paramId, value};
                UartSendFrame(0x08, ack_data, 3);  // Param ACK
            }
            return true;
        }

        case 0x09:  // Request Knob Values
            SendKnobValues();
            return true;

        case 0x0A:  // Arm Recording
            ArmRecording();
            return true;

        case 0x0B:  // Disarm Recording
            DisarmRecording();
            return true;

        case 0x0C:  // Backing Track
            backingTrackRecordBlend = (payload[1] != 0);
            backingTrackBlendRatio = payload[2];
            backingTrackBlendMic = (payload[3] != 0);
            return true;

        case 0x0D:  // Set Bypass
        {
            bypass = (payload[1] != 0);
            uint8_t data[] = {bypass ? (uint8_t)1 : (uint8_t)0};
            UartSendFrame(0x0D, data, 1);  // Bypass Status ACK
            return true;
        }

        case 0xFF:  // Select Effect
            EnableEffect(payload[1]);
            return true;

        case 0x10:  // Control Change (Legacy MIDI CC)
        {
            uint8_t ccNum = payload[1];
            uint8_t ccVal = payload[2];
            bkshepherd::BaseEffectModule* module = GetEffectModule(static_cast<uint8_t>(enabled_effect));
            if (module != nullptr) {
                module->MidiCCValueNotification(ccNum, ccVal);
            }
            return true;
        }

        default:
            return false;  // Unrecognized command
    }
}
