#ifndef B0E1E42C_0775_414D_9DF3_6644EACE2545
#define B0E1E42C_0775_414D_9DF3_6644EACE2545
#include "driver/uart.h"
#include "server.h"
#include "string.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#define TXD_PIN (GPIO_NUM_6)
#define RXD_PIN (GPIO_NUM_5)

static const int RX_BUF_SIZE = 128 + 1;  // UART FIFO length for ESP32-S3
const int UART_RECORDING_INDEX = 0;
const int UART_PROGRAM_CHANGE_INDEX = 1;
const int UART_CONTROL_CHANGE_INDEX = 2;

// UART Protocol Commands - TX (to Daisy)
#define UART_CMD_STOP_RECORDING    1
#define UART_CMD_START_RECORDING   2
#define UART_CMD_RESET_BOOTLOADER  3
#define UART_CMD_SET_PARAMETER     8
#define UART_CMD_REQUEST_KNOBS     9
#define UART_CMD_ARM_RECORDING     10
#define UART_CMD_DISARM            11
#define UART_CMD_BACKING_TRACK     12  // Backing track: data[0]=record_blend, data[1]=blend ratio, data[2]=blend_mic
#define UART_CMD_CONTROL_CHANGE    16
#define UART_CMD_SELECT_EFFECT     255

// UART Protocol Commands - RX (from Daisy)
#define UART_RX_RECORDING_STATUS   1
#define UART_RX_SYNC_REQUEST       3
#define UART_RX_FW_VERSION         4
#define UART_RX_ACK_BOOTLOADER     5
#define UART_RX_TOGGLE_VALUES      6
#define UART_RX_EFFECT_SWITCHED    7
#define UART_RX_PARAM_ACK          8
#define UART_RX_KNOB_VALUES        9
#define UART_RX_ARM_ACK            10

// Effect Indices
#define EFFECT_GALAXY      0
#define EFFECT_REVERB      1
#define EFFECT_AMPSIM      2
#define EFFECT_NAM         3
#define EFFECT_DISTORTION  4
#define EFFECT_DELAY       5
#define EFFECT_TREMOLO     6
#define EFFECT_CHORUS      7

// Global Parameters (param_id 0-3) - effect index ignored
#define PARAM_OUTPUT_BLEND_MODE    0
#define PARAM_GALAXYLITE_DAMPING   1
#define PARAM_GALAXYLITE_PREDELAY  2
#define PARAM_GALAXYLITE_MIX       3

// Output Blend Mode values
#define BLEND_STEREO       0
#define BLEND_MONO_CENTER  1
#define BLEND_MONO_LEFT    2
#define BLEND_MONO_RIGHT   3
// Values 4-255 are variable L/R blend ratios

// Protocol v2.0 framing constants
#define UART_START_BYTE        0xAA
#define UART_MAX_DATA_LEN      8     // Max data bytes (CMD + up to 7 payload bytes)
#define UART_MAX_PACKET_LEN    11    // START + LEN + CMD + 7 DATA + CHECKSUM
#define UART_RX_TIMEOUT_MS     100   // Timeout for complete packet reception

// Legacy buffer sizes (kept for transition)
#define cmd_buffer_size 8
#define rx_buffer_size 9  // Extra byte for null terminator in rx_task
uint8_t cmd_buffer[cmd_buffer_size];
uint8_t uart_recv_buffer[rx_buffer_size];

// Framed protocol buffers
uint8_t tx_frame_buffer[UART_MAX_PACKET_LEN];
uint8_t rx_frame_buffer[UART_MAX_PACKET_LEN];
bool seed_dfu = false;

// Calculate XOR checksum over LEN, CMD, and DATA bytes
static uint8_t uart_calculate_checksum(uint8_t len, uint8_t *payload) {
  uint8_t checksum = len;
  for (int i = 0; i < len; i++) {
    checksum ^= payload[i];
  }
  return checksum;
}

// Send a framed packet: START + LEN + CMD + DATA + CHECKSUM
static int uart_send_frame(uint8_t cmd, uint8_t *data, uint8_t data_len) {
  uint8_t len = 1 + data_len;  // CMD + DATA
  uint8_t payload[UART_MAX_DATA_LEN];

  payload[0] = cmd;
  for (int i = 0; i < data_len && i < UART_MAX_DATA_LEN - 1; i++) {
    payload[i + 1] = data[i];
  }

  tx_frame_buffer[0] = UART_START_BYTE;
  tx_frame_buffer[1] = len;
  for (int i = 0; i < len; i++) {
    tx_frame_buffer[2 + i] = payload[i];
  }
  tx_frame_buffer[2 + len] = uart_calculate_checksum(len, payload);

  int total_len = 3 + len;  // START + LEN + payload + CHECKSUM
  int txBytes = uart_write_bytes(UART_NUM_2, tx_frame_buffer, total_len);
  // ESP_LOGI("HOOPI", "TX frame [%d bytes]: %02x %02x %02x %02x %02x %02x %02x %02x",
  //          txBytes, tx_frame_buffer[0], tx_frame_buffer[1], tx_frame_buffer[2],
  //          tx_frame_buffer[3], tx_frame_buffer[4], tx_frame_buffer[5],
  //          tx_frame_buffer[6], tx_frame_buffer[7]);
  return txBytes;
}

// Send a simple command with no data
static int uart_send_cmd(uint8_t cmd) {
  return uart_send_frame(cmd, NULL, 0);
}

void clearTxBuffer() {
  for (int i = 0; i < cmd_buffer_size; i++) {
    cmd_buffer[i] = 255;
  }
}

void clearRxBuffer() {
  for (int i = 0; i < rx_buffer_size; i++) {
    uart_recv_buffer[i] = 255;
  }
}

void init_uart() {
  const uart_config_t uart_config = {
      .baud_rate = 31250,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
  uart_param_config(UART_NUM_2, &uart_config);
  uart_set_pin(UART_NUM_2, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  clearTxBuffer();
  clearRxBuffer();
}

#define PROGRAM_NAMESPACE "program"
uint8_t programId;

uint8_t controlNumber1;
uint8_t controlValue1;
uint8_t controlNumber2;
uint8_t controlValue2;
uint8_t controlNumber3;
uint8_t controlValue3;
uint8_t controlNumber4;
uint8_t controlValue4;

// Global variable to store Daisy Seed firmware version
uint8_t seed_fw_version = 0;
uint8_t seed_toggle1 = 255;
uint8_t seed_toggle2 = 255;
uint8_t seed_toggle3 = 255;
uint8_t esp32_fw_version = 4;

// Knob values from Daisy (0-255 each, scaled from 0.0-1.0)
uint8_t seed_knob_values[6] = {0, 0, 0, 0, 0, 0};
uint8_t seed_knob_effect = 255;  // Effect ID from knob response (bits 0-3 of byte7)
uint8_t seed_knob_toggle = 255;  // Toggle state from knob response (bits 4-7 of byte7)
volatile bool knob_values_received = false;

// Initial sync flag - sync params to Daisy when it's ready (sends toggle values)
volatile bool initial_sync_done = false;

// Global parameters (persisted to NVS, sent to Daisy on startup)
// Defaults from effects_config.json
uint8_t global_blend_mode = 1;           // param_id 0: Mono Center
uint8_t global_galaxylite_damping = 140; // param_id 1
uint8_t global_galaxylite_predelay = 128;// param_id 2
uint8_t global_galaxylite_mix = 77;      // param_id 3
uint8_t global_comp_threshold = 102;     // param_id 4
uint8_t global_comp_ratio = 40;          // param_id 5
uint8_t global_comp_attack = 5;          // param_id 6
uint8_t global_comp_release = 12;        // param_id 7
uint8_t global_comp_makeup = 85;         // param_id 8
uint8_t global_gate_threshold = 128;     // param_id 9
uint8_t global_gate_attack = 5;          // param_id 10
uint8_t global_gate_hold = 26;           // param_id 11
uint8_t global_gate_release = 12;        // param_id 12
uint8_t global_blend_apply_to_rec = 0;   // extra byte for blend mode

// EQ parameters (param_id 30-36)
uint8_t global_eq_enable = 0;            // param_id 30: 0=Off, 1=On
uint8_t global_eq_low_gain = 128;        // param_id 31: 128=0dB
uint8_t global_eq_mid_gain = 128;        // param_id 32: 128=0dB
uint8_t global_eq_high_gain = 128;       // param_id 33: 128=0dB
uint8_t global_eq_low_freq = 85;         // param_id 34: ~200Hz
uint8_t global_eq_mid_freq = 51;         // param_id 35: ~1kHz
uint8_t global_eq_high_freq = 64;        // param_id 36: ~4kHz

// Effect-specific parameters (param_id 14-24, per effect)
// 255 = not set (use effect's default)
// Index: effect_params[effect_id][param_id - 14]
#define EFFECT_PARAM_BASE 14
#define EFFECT_PARAM_COUNT 11  // params 14-24
uint8_t effect_params[8][EFFECT_PARAM_COUNT];

// Initialize effect params array to 255 (not set)
void init_effect_params() {
    for (int e = 0; e < 8; e++) {
        for (int p = 0; p < EFFECT_PARAM_COUNT; p++) {
            effect_params[e][p] = 255;
        }
    }
}

// UART-only param_ids per effect (from effects_config.json)
// These are params that can ONLY be set via UART/API, not physical knobs
// Format: {count, param_id1, param_id2, ...} - max 8 params per effect
const uint8_t uart_only_params[8][9] = {
    {0},                        // 0: Galaxy - no uart_only params
    {3, 17, 18, 20},            // 1: CloudSeed - Mod Amt, Mod Rate, Preset
    {5, 15, 17, 19, 20, 21},    // 2: AmpSim - Mix, Tone, IR, NeuralModel, IR On
    {5, 17, 18, 19, 20, 21},    // 3: NAM - Bass, Mid, Treble, NeuralModel, EQ
    {0},                        // 4: Distortion - uart_only params have no midi_cc
    {1, 18},                    // 5: Delay - Tone
    {0},                        // 6: Tremolo - no uart_only params
    {0},                        // 7: Chorus - no uart_only params
};

// Check if a param_id is settable via UART for a given effect
// Returns true for global params (0-12, 30-36) and uart_only effect params
bool is_param_settable(uint8_t effect_idx, uint8_t param_id) {
    // Global params are always settable (0-12 and 30-36 for EQ)
    if (param_id <= 12 || (param_id >= 30 && param_id <= 36)) {
        return true;
    }

    // Check uart_only params for this effect
    if (effect_idx < 8) {
        uint8_t count = uart_only_params[effect_idx][0];
        for (uint8_t i = 1; i <= count; i++) {
            if (uart_only_params[effect_idx][i] == param_id) {
                return true;
            }
        }
    }

    return false;
}

// Last parameter ACK received from Daisy
volatile uint8_t last_param_ack_effect = 255;
volatile uint8_t last_param_ack_param = 255;
volatile uint8_t last_param_ack_value = 255;
volatile bool param_ack_received = false;
volatile bool effect_ack_received = false;
volatile bool arm_ack_received = false;

static void save_program_config() {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(PROGRAM_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error accessing NVS %d", err);
  }

  err = nvs_set_u8(nvs_handle, "programId", programId);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving programId %d", err);
  }

  err = nvs_set_u8(nvs_handle, "controlNumber1", controlNumber1);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving controlNumber1 %d", err);
  }

  err = nvs_set_u8(nvs_handle, "controlNumber2", controlNumber2);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving controlNumber2 %d", err);
  }

  err = nvs_set_u8(nvs_handle, "controlNumber3", controlNumber3);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving controlNumber3 %d", err);
  }

  err = nvs_set_u8(nvs_handle, "controlNumber4", controlNumber4);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving controlNumber4 %d", err);
  }

  err = nvs_set_u8(nvs_handle, "controlValue1", controlValue1);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving controlValue1 %d", err);
  }

  err = nvs_set_u8(nvs_handle, "controlValue2", controlValue2);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving controlValue2 %d", err);
  }

  err = nvs_set_u8(nvs_handle, "controlValue3", controlValue3);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving controlValue3 %d", err);
  }

  err = nvs_set_u8(nvs_handle, "controlValue4", controlValue4);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error saving controlValue4 %d", err);
  }

  // Save global parameters
  nvs_set_u8(nvs_handle, "blendMode", global_blend_mode);
  nvs_set_u8(nvs_handle, "blendApplyRec", global_blend_apply_to_rec);
  nvs_set_u8(nvs_handle, "glDamping", global_galaxylite_damping);
  nvs_set_u8(nvs_handle, "glPredelay", global_galaxylite_predelay);
  nvs_set_u8(nvs_handle, "glMix", global_galaxylite_mix);
  nvs_set_u8(nvs_handle, "compThresh", global_comp_threshold);
  nvs_set_u8(nvs_handle, "compRatio", global_comp_ratio);
  nvs_set_u8(nvs_handle, "compAttack", global_comp_attack);
  nvs_set_u8(nvs_handle, "compRelease", global_comp_release);
  nvs_set_u8(nvs_handle, "compMakeup", global_comp_makeup);
  nvs_set_u8(nvs_handle, "gateThresh", global_gate_threshold);
  nvs_set_u8(nvs_handle, "gateAttack", global_gate_attack);
  nvs_set_u8(nvs_handle, "gateHold", global_gate_hold);
  nvs_set_u8(nvs_handle, "gateRelease", global_gate_release);

  // Save EQ parameters
  nvs_set_u8(nvs_handle, "eqEnable", global_eq_enable);
  nvs_set_u8(nvs_handle, "eqLowGain", global_eq_low_gain);
  nvs_set_u8(nvs_handle, "eqMidGain", global_eq_mid_gain);
  nvs_set_u8(nvs_handle, "eqHighGain", global_eq_high_gain);
  nvs_set_u8(nvs_handle, "eqLowFreq", global_eq_low_freq);
  nvs_set_u8(nvs_handle, "eqMidFreq", global_eq_mid_freq);
  nvs_set_u8(nvs_handle, "eqHighFreq", global_eq_high_freq);

  // Save effect-specific parameters (only non-255 values)
  char key[12];
  for (int e = 0; e < 8; e++) {
    for (int p = 0; p < EFFECT_PARAM_COUNT; p++) {
      if (effect_params[e][p] != 255) {
        snprintf(key, sizeof(key), "e%dp%d", e, p + EFFECT_PARAM_BASE);
        nvs_set_u8(nvs_handle, key, effect_params[e][p]);
      }
    }
  }

  nvs_commit(nvs_handle);
  nvs_close(nvs_handle);
}

static void load_program_config() {
  // Initialize effect params to 255 (not set)
  init_effect_params();

  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(PROGRAM_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE("Hoopi", "Error accessing NVS %d", err);
    return;
  }


  err = nvs_get_u8(nvs_handle, "programId", &programId);
  if (err != ESP_OK) {
      programId = 0;
  }

  err = nvs_get_u8(nvs_handle, "controlNumber1", &controlNumber1);
  if (err != ESP_OK) {
      controlNumber1 = 255;
  }

  err = nvs_get_u8(nvs_handle, "controlNumber2", &controlNumber2);
  if (err != ESP_OK) {
      controlNumber2 = 255;
  }

  err = nvs_get_u8(nvs_handle, "controlNumber3", &controlNumber3);
  if (err != ESP_OK) {
      controlNumber3 = 255;
  }

  err = nvs_get_u8(nvs_handle, "controlNumber4", &controlNumber4);
  if (err != ESP_OK) {
      controlNumber4 = 255;
  }

  err = nvs_get_u8(nvs_handle, "controlValue1", &controlValue1);
  if (err != ESP_OK) {
      controlValue1 = 255;
  }

  err = nvs_get_u8(nvs_handle, "controlValue2", &controlValue2);
  if (err != ESP_OK) {
      controlValue2 = 255;
  }

  err = nvs_get_u8(nvs_handle, "controlValue3", &controlValue3);
  if (err != ESP_OK) {
      controlValue3 = 255;
  }

  err = nvs_get_u8(nvs_handle, "controlValue4", &controlValue4);
  if (err != ESP_OK) {
      controlValue4 = 255;
  }

  // Load global parameters (keep defaults if not found)
  nvs_get_u8(nvs_handle, "blendMode", &global_blend_mode);
  nvs_get_u8(nvs_handle, "blendApplyRec", &global_blend_apply_to_rec);
  nvs_get_u8(nvs_handle, "glDamping", &global_galaxylite_damping);
  nvs_get_u8(nvs_handle, "glPredelay", &global_galaxylite_predelay);
  nvs_get_u8(nvs_handle, "glMix", &global_galaxylite_mix);
  nvs_get_u8(nvs_handle, "compThresh", &global_comp_threshold);
  nvs_get_u8(nvs_handle, "compRatio", &global_comp_ratio);
  nvs_get_u8(nvs_handle, "compAttack", &global_comp_attack);
  nvs_get_u8(nvs_handle, "compRelease", &global_comp_release);
  nvs_get_u8(nvs_handle, "compMakeup", &global_comp_makeup);
  nvs_get_u8(nvs_handle, "gateThresh", &global_gate_threshold);
  nvs_get_u8(nvs_handle, "gateAttack", &global_gate_attack);
  nvs_get_u8(nvs_handle, "gateHold", &global_gate_hold);
  nvs_get_u8(nvs_handle, "gateRelease", &global_gate_release);

  // Load EQ parameters
  nvs_get_u8(nvs_handle, "eqEnable", &global_eq_enable);
  nvs_get_u8(nvs_handle, "eqLowGain", &global_eq_low_gain);
  nvs_get_u8(nvs_handle, "eqMidGain", &global_eq_mid_gain);
  nvs_get_u8(nvs_handle, "eqHighGain", &global_eq_high_gain);
  nvs_get_u8(nvs_handle, "eqLowFreq", &global_eq_low_freq);
  nvs_get_u8(nvs_handle, "eqMidFreq", &global_eq_mid_freq);
  nvs_get_u8(nvs_handle, "eqHighFreq", &global_eq_high_freq);

  // Load effect-specific parameters
  char key[12];
  int effect_params_loaded = 0;
  for (int e = 0; e < 8; e++) {
    for (int p = 0; p < EFFECT_PARAM_COUNT; p++) {
      snprintf(key, sizeof(key), "e%dp%d", e, p + EFFECT_PARAM_BASE);
      if (nvs_get_u8(nvs_handle, key, &effect_params[e][p]) == ESP_OK) {
        effect_params_loaded++;
      }
    }
  }

  ESP_LOGI("HOOPI", "Loaded config: effect=%d, blend=%d, glDamp=%d, glMix=%d, effectParams=%d",
           programId, global_blend_mode, global_galaxylite_damping, global_galaxylite_mix, effect_params_loaded);

  nvs_close(nvs_handle);
}

int sendData(const uint8_t* data, int len)
{
    const int txBytes = uart_write_bytes(UART_NUM_2, data, len);
    // ESP_LOGI(TX_TASK_TAG, "Wrote %d bytes", txBytes);
    // ESP_LOG_BUFFER_HEXDUMP(TX_TASK_TAG, data, txBytes, ESP_LOG_INFO);
    clearTxBuffer();
    return txBytes;
}

#define SYNC_ACK_TIMEOUT_MS 1000
#define SYNC_MAX_RETRIES 5

// Send a param and wait for ACK with retries
// Returns true if ACK received, false if failed after all retries
static bool send_param_with_ack(uint8_t effect_idx, uint8_t param_id, uint8_t value, uint8_t extra) {
  for (int retry = 0; retry < SYNC_MAX_RETRIES; retry++) {
    param_ack_received = false;
    uint8_t data[] = {effect_idx, param_id, value, extra};
    uart_send_frame(UART_CMD_SET_PARAMETER, data, 4);

    // Wait for ACK with timeout
    int wait_ms = 0;
    while (!param_ack_received && wait_ms < SYNC_ACK_TIMEOUT_MS) {
      vTaskDelay(pdMS_TO_TICKS(10));
      wait_ms += 10;
    }

    if (param_ack_received) {
      return true;  // Success
    }

    ESP_LOGW("HOOPI", "Param ACK timeout (effect=%d, param=%d), retry %d/%d",
             effect_idx, param_id, retry + 1, SYNC_MAX_RETRIES);
  }

  return false;  // Failed after all retries
}

static void send_sync(){
  int sync_failures = 0;

  // Send effect selection and wait for ACK (cmd=7)
  effect_ack_received = false;
  uint8_t effect_data[] = {programId};
  uart_send_frame(UART_CMD_SELECT_EFFECT, effect_data, 1);

  // Wait for effect ACK (cmd=7) - Daisy responds with current effect
  int wait_ms = 0;
  while (!effect_ack_received && wait_ms < SYNC_ACK_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(50));
    wait_ms += 50;
  }
  if (!effect_ack_received) {
    ESP_LOGW("HOOPI", "Effect selection ACK timeout");
  }

  // Send control changes (no ACK for these)
  if (controlNumber1 != 255)
  {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t cc_data[] = {controlNumber1, controlValue1};
    uart_send_frame(UART_CMD_CONTROL_CHANGE, cc_data, 2);
  }

  if (controlNumber2 != 255)
  {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t cc_data[] = {controlNumber2, controlValue2};
    uart_send_frame(UART_CMD_CONTROL_CHANGE, cc_data, 2);
  }

  if (controlNumber3 != 255)
  {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t cc_data[] = {controlNumber3, controlValue3};
    uart_send_frame(UART_CMD_CONTROL_CHANGE, cc_data, 2);
  }

  if (controlNumber4 != 255)
  {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t cc_data[] = {controlNumber4, controlValue4};
    uart_send_frame(UART_CMD_CONTROL_CHANGE, cc_data, 2);
  }

  // Send global parameters with ACK verification
  // ESP_LOGI("HOOPI", "Syncing global params to Daisy...");

  // First param - abort if this fails (no point continuing)
  if (!send_param_with_ack(0, 0, global_blend_mode, global_blend_apply_to_rec)) {
    ESP_LOGE("HOOPI", "*** SYNC FAILED: Cannot communicate with Daisy (first param failed after %d retries) ***", SYNC_MAX_RETRIES);
    return;
  }

  // Continue with remaining params
  if (!send_param_with_ack(0, 1, global_galaxylite_damping, 0)) sync_failures++;
  if (!send_param_with_ack(0, 2, global_galaxylite_predelay, 0)) sync_failures++;
  if (!send_param_with_ack(0, 3, global_galaxylite_mix, 0)) sync_failures++;
  if (!send_param_with_ack(0, 4, global_comp_threshold, 0)) sync_failures++;
  if (!send_param_with_ack(0, 5, global_comp_ratio, 0)) sync_failures++;
  if (!send_param_with_ack(0, 6, global_comp_attack, 0)) sync_failures++;
  if (!send_param_with_ack(0, 7, global_comp_release, 0)) sync_failures++;
  if (!send_param_with_ack(0, 8, global_comp_makeup, 0)) sync_failures++;
  if (!send_param_with_ack(0, 9, global_gate_threshold, 0)) sync_failures++;
  if (!send_param_with_ack(0, 10, global_gate_attack, 0)) sync_failures++;
  if (!send_param_with_ack(0, 11, global_gate_hold, 0)) sync_failures++;
  if (!send_param_with_ack(0, 12, global_gate_release, 0)) sync_failures++;

  // EQ params (30-36)
  if (!send_param_with_ack(0, 30, global_eq_enable, 0)) sync_failures++;
  if (!send_param_with_ack(0, 31, global_eq_low_gain, 0)) sync_failures++;
  if (!send_param_with_ack(0, 32, global_eq_mid_gain, 0)) sync_failures++;
  if (!send_param_with_ack(0, 33, global_eq_high_gain, 0)) sync_failures++;
  if (!send_param_with_ack(0, 34, global_eq_low_freq, 0)) sync_failures++;
  if (!send_param_with_ack(0, 35, global_eq_mid_freq, 0)) sync_failures++;
  if (!send_param_with_ack(0, 36, global_eq_high_freq, 0)) sync_failures++;

  ESP_LOGI("HOOPI", "Global params sync complete");

  // Send effect-specific parameters with ACK verification
  // ESP_LOGI("HOOPI", "Syncing effect-specific params to Daisy...");
  int effect_params_sent = 0;
  for (int e = 0; e < 8; e++) {
    for (int p = 0; p < EFFECT_PARAM_COUNT; p++) {
      if (effect_params[e][p] != 255) {
        if (!send_param_with_ack(e, p + EFFECT_PARAM_BASE, effect_params[e][p], 0)) {
          sync_failures++;
        }
        effect_params_sent++;
      }
    }
  }
  ESP_LOGI("HOOPI", "Effect params sync complete (%d params sent)", effect_params_sent);

  // Report sync status
  if (sync_failures > 0) {
    ESP_LOGW("HOOPI", "*** SYNC WARNING: %d params failed to sync after %d retries ***",
             sync_failures, SYNC_MAX_RETRIES);
  } else {
    ESP_LOGI("HOOPI", "*** SYNC SUCCESS: All params synced to Daisy ***");
  }
}

// Task wrapper for send_sync to avoid deadlock when called from rx_task
static void sync_task(void *arg) {
  // Delay to let Daisy's DMA re-arm and main loop settle
  // (Daisy has a race condition where recv_buffer can be overwritten before processing)
  vTaskDelay(pdMS_TO_TICKS(500));
  send_sync();
  vTaskDelete(NULL);  // Self-delete when done
}

void store_seed_fw_version(uint8_t version) {
  seed_fw_version = version;
  ESP_LOGI("HOOPI", "Daisy Seed firmware version received: %d", version);
}

void store_seed_toggle_values(uint8_t toggle1, uint8_t toggle2, uint8_t toggle3) {
  seed_toggle1 = toggle1;
  seed_toggle2 = toggle2;
  seed_toggle3 = toggle3;
  ESP_LOGI("HOOPI", "Daisy Seed toggle values received: %d, %d, %d", toggle1, toggle2, toggle3);
}

void store_seed_effect(uint8_t id) {
  programId = id;
  effect_ack_received = true;
  save_program_config();
  ESP_LOGI("HOOPI", "Daisy Seed active effect: %d", id);
}

void store_param_ack(uint8_t effect_idx, uint8_t param_id, uint8_t value) {
  last_param_ack_effect = effect_idx;
  last_param_ack_param = param_id;
  last_param_ack_value = value;
  param_ack_received = true;
  // ESP_LOGI("HOOPI", "Param ACK: effect=%d, param=%d, value=%d", effect_idx, param_id, value);
}

void store_knob_values(uint8_t *knobs, uint8_t byte7) {
  for (int i = 0; i < 6; i++) {
    seed_knob_values[i] = knobs[i];
  }
  // byte7: effect (bits 0-3) | toggle (bits 4-7)
  seed_knob_effect = byte7 & 0x0F;
  seed_knob_toggle = byte7 >> 4;
  knob_values_received = true;
  ESP_LOGI("HOOPI", "Knob values: %d, %d, %d, %d, %d, %d (effect=%d, toggle=%d)",
           knobs[0], knobs[1], knobs[2], knobs[3], knobs[4], knobs[5],
           seed_knob_effect, seed_knob_toggle);
}


// Process a validated command from the framed protocol
static void uart_process_command(uint8_t cmd, uint8_t *data, uint8_t data_len) {
    switch (cmd) {
      case UART_RX_RECORDING_STATUS:
        if (data_len >= 1) {
          if (data[0] == 2) {
            ESP_LOGI("HOOPI", "Start record command received.");
            // If recording is armed (waiting for footswitch), use the armed filename
            if (is_recording_armed) {
              ESP_LOGI("HOOPI", "Recording was armed, using preset filename: %s", new_filename);
              user_specified_filename = true;
              is_recording_armed = false;  // Clear armed state
            }
            start_recording();
          } else if (data[0] == 1) {
            stop_recording();
          }
        }
        break;

      case UART_RX_SYNC_REQUEST:
        ESP_LOGI("HOOPI", "Daisy sync request received, scheduling sync...");
        xTaskCreate(sync_task, "sync_task", 4096, NULL, 5, NULL);
        break;

      case UART_RX_FW_VERSION:
        if (data_len >= 1) {
          seed_dfu = false;
          store_seed_fw_version(data[0]);
        }
        break;

      case UART_RX_ACK_BOOTLOADER:
        ESP_LOGI("HOOPI", "Daisy Seed is in DFU mode.");
        seed_dfu = true;
        break;

      case UART_RX_TOGGLE_VALUES:
        if (data_len >= 3) {
          store_seed_toggle_values(data[0], data[1], data[2]);
          if (!initial_sync_done) {
            ESP_LOGI("HOOPI", "Daisy ready (toggle values received), scheduling initial sync...");
            initial_sync_done = true;
            xTaskCreate(sync_task, "sync_task", 4096, NULL, 5, NULL);
          }
        }
        break;

      case UART_RX_EFFECT_SWITCHED:
        if (data_len >= 1) {
          store_seed_effect(data[0]);
        }
        break;

      case UART_RX_PARAM_ACK:
        if (data_len >= 3) {
          store_param_ack(data[0], data[1], data[2]);
        }
        break;

      case UART_RX_KNOB_VALUES:
        if (data_len >= 7) {
          store_knob_values(data, data[6]);
        }
        break;

      case UART_RX_ARM_ACK:
        ESP_LOGI("HOOPI", "Recording arm ACK received from Daisy");
        arm_ack_received = true;
        break;

      default:
        ESP_LOGW("HOOPI", "Unknown UART cmd: %d", cmd);
        break;
    }
}

static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "HOOPI-RX";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);

    uint8_t byte;
    uint8_t payload[UART_MAX_DATA_LEN];

    while (1) {
        // Wait for START byte
        if (uart_read_bytes(UART_NUM_2, &byte, 1, pdMS_TO_TICKS(100)) == 1) {
            if (byte != UART_START_BYTE) {
                // Not a start byte, skip
                continue;
            }

            // Read LEN byte with timeout
            if (uart_read_bytes(UART_NUM_2, &byte, 1, pdMS_TO_TICKS(UART_RX_TIMEOUT_MS)) != 1) {
                ESP_LOGW(RX_TASK_TAG, "Timeout waiting for LEN byte");
                continue;
            }
            uint8_t len = byte;

            // Validate length (1-8 bytes: CMD + up to 7 data bytes)
            if (len < 1 || len > UART_MAX_DATA_LEN) {
                ESP_LOGW(RX_TASK_TAG, "Invalid packet length: %d", len);
                continue;
            }

            // Read payload (CMD + DATA)
            int bytes_read = uart_read_bytes(UART_NUM_2, payload, len, pdMS_TO_TICKS(UART_RX_TIMEOUT_MS));
            if (bytes_read != len) {
                ESP_LOGW(RX_TASK_TAG, "Timeout reading payload: got %d, expected %d", bytes_read, len);
                continue;
            }

            // Read CHECKSUM byte
            uint8_t received_checksum;
            if (uart_read_bytes(UART_NUM_2, &received_checksum, 1, pdMS_TO_TICKS(UART_RX_TIMEOUT_MS)) != 1) {
                ESP_LOGW(RX_TASK_TAG, "Timeout waiting for checksum");
                continue;
            }

            // Validate checksum
            uint8_t calculated_checksum = uart_calculate_checksum(len, payload);
            if (received_checksum != calculated_checksum) {
                ESP_LOGW(RX_TASK_TAG, "Checksum mismatch: got 0x%02x, expected 0x%02x", received_checksum, calculated_checksum);
                continue;
            }

            // Packet valid - extract command and data
            uint8_t cmd = payload[0];
            uint8_t *data = &payload[1];
            uint8_t data_len = len - 1;

            // ESP_LOGI(RX_TASK_TAG, "RX valid frame: cmd=0x%02x, data_len=%d", cmd, data_len);

            // Process the command
            uart_process_command(cmd, data, data_len);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void send_start_rec_cmd() {
  uart_send_cmd(UART_CMD_START_RECORDING);
}

void send_stop_rec_cmd() {
  uart_send_cmd(UART_CMD_STOP_RECORDING);
}

void send_dfu_cmd() {
  uart_send_cmd(UART_CMD_RESET_BOOTLOADER);
}

void send_program_change_cmd(uint8_t newId, bool clear) {
  uint8_t data[] = {newId};
  uart_send_frame(UART_CMD_SELECT_EFFECT, data, 1);

  programId = newId;
  if (clear) {
    controlNumber1 = controlNumber2 = controlNumber3 = controlNumber4 = controlValue1 = controlValue2 = controlValue3 = controlValue4 = 255;
  }
}

void send_control_change_cmd(uint8_t control_number, uint8_t value) {
  uint8_t data[] = {control_number, value};
  uart_send_frame(UART_CMD_CONTROL_CHANGE, data, 2);

  //find an unused CC slot
  if (controlNumber1 == 255 || controlNumber1 == control_number) {
    controlNumber1 = control_number;
    controlValue1 = value;
  }
  else if (controlNumber2 == 255 || controlNumber2 == control_number) {
    controlNumber2 = control_number;
    controlValue2 = value;
  }
  else if (controlNumber3 == 255 || controlNumber3 == control_number) {
    controlNumber3 = control_number;
    controlValue3 = value;
  }
  else if (controlNumber4 == 255 || controlNumber4 == control_number) {
    controlNumber4 = control_number;
    controlValue4 = value;
  }
  else {
    //no slots left for this program
    ESP_LOGW("HOOPI", "No CC slots left.");
  }

  save_program_config();
}

// Select effect command (cmd=255)
void send_select_effect_cmd(uint8_t effect_id) {
  uint8_t data[] = {effect_id};
  uart_send_frame(UART_CMD_SELECT_EFFECT, data, 1);
  ESP_LOGI("HOOPI", "Select effect cmd sent: effect=%d", effect_id);
}

// Set parameter command (cmd=8)
void send_set_parameter_cmd(uint8_t effect_idx, uint8_t param_id, uint8_t value, uint8_t extra) {
  param_ack_received = false;
  uint8_t data[] = {effect_idx, param_id, value, extra};
  uart_send_frame(UART_CMD_SET_PARAMETER, data, 4);
  ESP_LOGI("HOOPI", "Set param cmd sent: effect=%d, param=%d, value=%d, extra=%d",
           effect_idx, param_id, value, extra);
}

// Convenience function for setting parameter without extra byte
void send_set_parameter_cmd_simple(uint8_t effect_idx, uint8_t param_id, uint8_t value) {
  send_set_parameter_cmd(effect_idx, param_id, value, 0);
}

// Request knob values from Daisy (cmd=9)
void send_request_knob_values() {
  knob_values_received = false;
  uart_send_cmd(UART_CMD_REQUEST_KNOBS);
  ESP_LOGI("HOOPI", "Request knob values cmd sent");
}

// Arm recording command (cmd=10)
void send_arm_recording_cmd() {
  arm_ack_received = false;
  uart_send_cmd(UART_CMD_ARM_RECORDING);
  ESP_LOGI("HOOPI", "Arm recording cmd sent");
}

// Disarm command (cmd=11) - cancel armed state on Daisy
void send_disarm_cmd() {
  uart_send_cmd(UART_CMD_DISARM);
  ESP_LOGI("HOOPI", "Disarm cmd sent");
}

// Set output blend mode (global parameter, effect_idx ignored)
void send_set_blend_mode_cmd(uint8_t blend_mode, uint8_t apply_to_recording) {
  send_set_parameter_cmd(0, PARAM_OUTPUT_BLEND_MODE, blend_mode, apply_to_recording);
}

// Set GalaxyLite damping (global parameter)
void send_set_galaxylite_damping_cmd(uint8_t damping) {
  send_set_parameter_cmd_simple(0, PARAM_GALAXYLITE_DAMPING, damping);
}

// Set GalaxyLite pre-delay (global parameter)
void send_set_galaxylite_predelay_cmd(uint8_t predelay) {
  send_set_parameter_cmd_simple(0, PARAM_GALAXYLITE_PREDELAY, predelay);
}

// Set GalaxyLite mix (global parameter)
void send_set_galaxylite_mix_cmd(uint8_t mix) {
  send_set_parameter_cmd_simple(0, PARAM_GALAXYLITE_MIX, mix);
}

// Backing track command (cmd=12)
// Sent when playback starts to configure how backing track audio is mixed.
// record_blend: 0=don't blend into recording, 1=blend into recording
// blend: 0-127 (0.0-0.5) - always applies to output, applies to recording if record_blend=1
// blend_mic: 0=guitar channel only, 1=also blend mic channel
void send_backing_track_cmd(uint8_t record_blend, uint8_t blend, uint8_t blend_mic) {
  uint8_t data[] = {record_blend, blend, blend_mic};
  uart_send_frame(UART_CMD_BACKING_TRACK, data, 3);
  ESP_LOGI("HOOPI", "Backing track cmd sent: record_blend=%d, blend=%d, blend_mic=%d", record_blend, blend, blend_mic);
}

#endif /* B0E1E42C_0775_414D_9DF3_6644EACE2545 */
