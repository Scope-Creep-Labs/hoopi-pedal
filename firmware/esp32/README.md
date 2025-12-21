# Hoopi ESP32 Firmware

ESP32-S3 firmware for the Hoopi guitar effects pedal. Handles audio recording, WiFi API, and UART communication with the Daisy Seed audio processor.

## Features

- **Audio Recording**: 48kHz/16-bit stereo WAV recording to SD card
- **WiFi API**: HTTP endpoints for recording control, file management, and effect parameters
- **UART Protocol**: Framed protocol (v2) with checksums for reliable communication with Daisy Seed
- **UDP Discovery**: Automatic device discovery on local network

## Requirements

- ESP-IDF v5.4 or later
- ESP32-S3 with PSRAM (Octal SPI)
- microSD card

## Build Instructions

### 1. Install ESP-IDF

Follow the [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) to install ESP-IDF v5.4.

### 2. Set up the environment

```bash
. ~/path/to/esp-idf/export.sh
```

### 3. Configure target

```bash
idf.py set-target esp32s3
```

### 4. Build

```bash
idf.py build
```

### 5. Flash

```bash
idf.py flash -p /dev/ttyUSB0
```

Replace `/dev/ttyUSB0` with your serial port.

### 6. Monitor

```bash
idf.py monitor -p /dev/ttyUSB0
```

Or combine flash and monitor:

```bash
idf.py flash monitor -p /dev/ttyUSB0
```

## Hardware Connections

| Function | GPIO |
|----------|------|
| UART TX (to Daisy) | 6 |
| UART RX (from Daisy) | 5 |
| I2S BCLK | 4 |
| I2S WS | 3 |
| I2S DIN | 2 |
| I2S DOUT | 1 |
| SD CLK | 7 |
| SD CMD | 9 |
| SD D0 | 8 |
| LED | 21 |

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/recording` | POST | Start/stop recording |
| `/api/list` | GET | List recordings |
| `/api/wav/{file}` | GET | Download recording |
| `/api/delete/{file}` | DELETE | Delete recording |
| `/api/status` | GET | Device status |
| `/api/setparam` | POST | Set effect parameter |
| `/api/selecteffect` | POST | Change active effect |
| `/api/params` | GET | Get all parameters |
| `/api/effects_config` | GET | Get effects configuration |
| `/api/wificonfig` | POST | Configure WiFi |
| `/api/wifiscan` | GET | Scan for networks |
| `/api/format` | POST | Format SD card |
| `/api/dfu` | POST | Enter DFU mode |

## License

MIT License - see [LICENSE.md](LICENSE.md)
