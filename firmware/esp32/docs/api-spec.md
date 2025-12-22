# HTTP API Specification

REST API for the Hoopi/Riffpod ESP32 device.

**Base URL**: `http://<device-ip>` (default AP mode: `http://192.168.4.1`)

---

## Playback API

Control audio file playback via I2S to Daisy. Supports backing track mode for playing along with previous recordings.

### POST /api/playback/play

Start playback of a WAV file from the SD card.

**Request Body:**
```json
{
  "filename": "recording.wav",
  "loop": false,
  "record_blend": false,
  "blend_ratio": 0.25,
  "blend_mic": false
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `filename` | string | Yes | - | WAV file on SD card |
| `loop` | boolean | No | false | Loop playback continuously |
| `record_blend` | boolean | No | false | Blend backing track into recording |
| `blend_ratio` | float | No | 0.25 | Blend amount: 0.0 (live only) to 0.5 (equal mix) |
| `blend_mic` | boolean | No | false | Also blend the mic channel from backing track |

**Response:**
```json
{
  "status": "ok",
  "duration_ms": 180000,
  "record_blend": false,
  "blend_ratio": 0.25,
  "blend_mic": false
}
```

**Errors:**
- `400`: Missing filename or invalid JSON
- `500`: Failed to start playback (file not found, invalid format)

---

### POST /api/playback/pause

Pause current playback. Audio output continues with silence.

**Request Body:** None

**Response:**
```json
{"status": "ok"}
```

**Errors:**
```json
{"status": "error", "message": "Not playing"}
```

---

### POST /api/playback/resume

Resume paused playback.

**Request Body:** None

**Response:**
```json
{"status": "ok"}
```

**Errors:**
```json
{"status": "error", "message": "Not paused"}
```

---

### POST /api/playback/stop

Stop playback and reset position.

**Request Body:** None

**Response:**
```json
{"status": "ok"}
```

---

### POST /api/playback/seek

Seek to a position in the current file.

**Request Body:**
```json
{
  "position_ms": 30000
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `position_ms` | integer | Yes | Target position in milliseconds |

**Response:**
```json
{"status": "ok", "position_ms": 30000}
```

**Errors:**
- `400`: Missing position_ms
- Returns error status if no file loaded

---

### GET /api/playback/status

Get current playback state and position.

**Response (playing or paused):**
```json
{
  "state": "playing",
  "filename": "recording.wav",
  "position_ms": 45000,
  "duration_ms": 180000,
  "loop": false,
  "record_blend": false,
  "blend_ratio": 0.25,
  "blend_mic": false
}
```

**Response (stopped):**
```json
{
  "state": "stopped"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `state` | string | `"playing"`, `"paused"`, or `"stopped"` |
| `filename` | string | Current file (only when not stopped) |
| `position_ms` | integer | Current position in milliseconds |
| `duration_ms` | integer | Total duration in milliseconds |
| `loop` | boolean | Whether looping is enabled |
| `record_blend` | boolean | Whether blending into recording |
| `blend_ratio` | float | Current blend ratio (0.0-0.5) |
| `blend_mic` | boolean | Whether mic channel is being blended |

---

### POST /api/playback/loop

Enable or disable loop mode during playback.

**Request Body:**
```json
{
  "loop": true
}
```

**Response:**
```json
{"status": "ok"}
```

---

## Blend Parameters

The backing track feature allows mixing a previously recorded track with live input.

| Parameter | Range | Description |
|-----------|-------|-------------|
| `blend_ratio` | 0.0 - 0.5 | Mix ratio. 0.0 = live signal only, 0.5 = equal mix (50% live, 50% backing) |
| `record_blend` | true/false | When true, the blended audio is recorded. When false, only the live signal is recorded. |
| `blend_mic` | true/false | When true, the mic channel (right) from the backing track is also blended with the live mic input. |

**Channel Mapping:**
- Left channel: Guitar (processed signal)
- Right channel: Mic

**Use Cases:**

1. **Playback only** - Listen to a full stereo recording (guitar + mic):
   ```json
   {"filename": "track.wav", "blend_ratio": 0.5, "record_blend": false, "blend_mic": true}
   ```

2. **Practice mode** - Hear backing guitar, record only live guitar:
   ```json
   {"filename": "track.wav", "blend_ratio": 0.25, "record_blend": false, "blend_mic": false}
   ```

3. **Layer recording** - Record live guitar mixed with backing guitar:
   ```json
   {"filename": "track.wav", "blend_ratio": 0.25, "record_blend": true, "blend_mic": false}
   ```

4. **Full mix recording** - Record both channels with backing:
   ```json
   {"filename": "track.wav", "blend_ratio": 0.25, "record_blend": true, "blend_mic": true}
   ```

---

## Audio Format

**Supported playback format:**
- WAV files only
- 16-bit PCM
- Stereo (2 channels)
- 48kHz sample rate

---
