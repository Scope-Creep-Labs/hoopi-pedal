# Recording Feature

Capture your playing with studio-quality audio recording, stored directly to SD card with wireless file transfer.

## Audio Quality

- **48kHz / 16-bit stereo** — CD-quality audio capture
- **WAV format** — Uncompressed, lossless recording
- **Zero-latency monitoring** — Record what you hear through the effects chain

## How It Works

1. **Press the footswitch** to start recording
2. **Play your riff** — the audio path remains unchanged
3. **Press again** to stop and save
4. **Transfer via WiFi** — download recordings to your phone or computer

## Wireless File Management

Access your recordings from any device on the same network:

- **Browse recordings** — View all saved files with duration and size
- **Stream or download** — Listen back or transfer to your DAW
- **Resumable downloads** — Large files download reliably over WiFi
- **Delete remotely** — Manage storage without touching the device

## Storage

- **SD card slot** — Use any microSD card
- **High-speed interface** — 40MHz SDMMC for reliable capture
- **Auto-naming** — Files are numbered sequentially with device ID
- **Custom filenames** — Name recordings via the API before capture

## Technical Specifications

| Specification | Value |
|---------------|-------|
| Sample Rate | 48,000 Hz |
| Bit Depth | 16-bit |
| Channels | 2 (Stereo) |
| Format | WAV (PCM) |
| Byte Rate | 192 KB/sec |
| Storage | microSD (FAT32) |

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/recording` | POST | Start or stop recording |
| `/api/list` | GET | List all recordings with metadata |
| `/api/wav/{filename}` | GET | Download a recording |
| `/api/delete/{filename}` | DELETE | Delete a recording |
| `/api/status` | GET | Current recording state and duration |
| `/api/format` | POST | Format SD card |

## Real-Time Status

The device broadcasts its state continuously:

```json
{
  "name": "Hoopi",
  "is_recording": true,
  "filename": "42_hoopi_A1B2.wav",
  "duration": 15,
  "error_count": 0
}
```

## Reliability Features

- **Large memory buffers** — 3MB+ PSRAM prevents audio dropouts
- **Double-buffered I2S** — Continuous capture without gaps
- **Error tracking** — Monitor buffer overflows (should be zero)
- **Minimum duration filter** — Auto-deletes accidental short recordings
