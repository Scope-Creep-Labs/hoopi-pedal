# Backing Tracks Upload Feature

Allow users to upload WAV backing tracks (in addition to using past recordings) for playback with blend functionality.

## Folder Structure

```
/sdcard/
├── recordings.json           # Existing - auto-recordings list
├── backing-tracks.json       # NEW - uploaded backing tracks list
├── backing-tracks/           # NEW - folder for uploaded files
│   ├── song1.wav
│   └── drums.wav
├── 1_hoopi_ABCD.wav          # Existing recordings
└── ...
```

## Supported Audio Formats

| Property | Accepted Values | Notes |
|----------|-----------------|-------|
| Sample Rate | 48kHz only | Other rates rejected (user must convert before upload) |
| Bit Depth | 8, 16, 24, 32-bit | All converted to 32-bit for I2S |
| Channels | Mono or Stereo | Mono duplicated to both L/R channels |
| Format | PCM only | No compressed formats |

---

## API Endpoints

### POST /api/backing-tracks/upload

Upload a WAV file to the backing tracks folder.

**Request:** Raw WAV bytes with headers:
- `X-Filename: song1.wav` - target filename
- `Content-Length: 12345678` - file size in bytes

**Validation:**
- Must be valid WAV format (check RIFF header)
- Must be PCM (audio_format = 1)
- Must be 48kHz sample rate
- Bit depth: 8, 16, 24, or 32-bit accepted
- Stereo or mono accepted

**Response:**
```json
{"status": "ok", "filename": "song1.wav", "duration_sec": 180}
```

**Errors:**
- `400`: Missing X-Filename header, invalid WAV format, wrong sample rate
- `500`: Failed to write file

---

### GET /api/backing-tracks

List all backing tracks.

**Response:** Contents of `backing-tracks.json`
```json
[
  {"name": "song1.wav", "size_kb": 5400, "duration_sec": 180, "modified_at": 1703721600}
]
```

---

### DELETE /api/backing-tracks/{filename}

Delete a backing track.

**Response:**
```json
{"status": "success", "deleted_file": "song1.wav"}
```

---

### POST /api/playback/play (Updated)

Add optional `source` field to specify backing tracks folder.

**Request:**
```json
{
  "filename": "song1.wav",
  "source": "backing-track",
  "loop": true,
  "blend_ratio": 0.25,
  "record_blend": false,
  "blend_mic": false
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `filename` | string | Yes | - | WAV file name |
| `source` | string | No | `"recording"` | `"recording"` or `"backing-track"` |
| `loop` | boolean | No | false | Loop playback |
| `blend_ratio` | float | No | 0.25 | Blend amount (0.0-0.5) |
| `record_blend` | boolean | No | false | Blend into recording |
| `blend_mic` | boolean | No | false | Also blend mic channel |

---

## Implementation

### Files to Modify

| File | Changes |
|------|---------|
| `main/sdcard.h` | Add `BACKING_TRACKS_FOLDER`, `init_backing_tracks_folder()`, `generate_backing_tracks_json()` |
| `main/server.h` | Add upload, list, delete handlers; update playback handler with `source` field |
| `main/playback.h` | Accept mono files, add mono-to-stereo and bit depth conversion |
| `main/hoopi.cpp` | Call `init_backing_tracks_folder()` on startup |

### sdcard.h Additions

```c
#define BACKING_TRACKS_FOLDER SD_MOUNT_POINT "/backing-tracks"

void init_backing_tracks_folder() {
    struct stat st;
    if (stat(BACKING_TRACKS_FOLDER, &st) != 0) {
        mkdir(BACKING_TRACKS_FOLDER, 0755);
    }
}

void generate_backing_tracks_json() {
    // Same pattern as generate_wav_json() but for BACKING_TRACKS_FOLDER
    // Write to SD_MOUNT_POINT "/backing-tracks.json"
}
```

### Upload Handler

```c
static esp_err_t handle_backing_track_upload(httpd_req_t *req) {
    // 1. Get filename from X-Filename header
    char filename[128];
    if (httpd_req_get_hdr_value_str(req, "X-Filename", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-Filename header");
        return ESP_FAIL;
    }

    // 2. Security check - no path traversal, must end in .wav
    if (strstr(filename, "..") || strstr(filename, "/")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    // 3. Read first 44 bytes (WAV header) and validate
    uint8_t header[44];
    httpd_req_recv(req, (char*)header, 44);
    if (!validate_wav_header(header)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid WAV format (must be 48kHz PCM)");
        return ESP_FAIL;
    }

    // 4. Open file and write header
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", BACKING_TRACKS_FOLDER, filename);
    FILE *f = fopen(filepath, "wb");
    fwrite(header, 44, 1, f);

    // 5. Stream remaining data to file using scratch buffer
    int remaining = req->content_len - 44;
    char *scratch = ((struct file_server_data *)req->user_ctx)->scratch;
    while (remaining > 0) {
        int recv_size = MIN(remaining, SCRATCH_BUFSIZE);
        int received = httpd_req_recv(req, scratch, recv_size);
        if (received <= 0) break;
        fwrite(scratch, received, 1, f);
        remaining -= received;
    }
    fclose(f);

    // 6. Generate backing_tracks.json
    generate_backing_tracks_json();

    // 7. Return success with duration
    // ...
}
```

### WAV Validation

```c
bool validate_wav_header(const uint8_t *header) {
    // Check RIFF signature
    if (memcmp(header, "RIFF", 4) != 0) return false;
    // Check WAVE format
    if (memcmp(header + 8, "WAVE", 4) != 0) return false;
    // Check fmt chunk
    if (memcmp(header + 12, "fmt ", 4) != 0) return false;

    // Read format info (little-endian)
    uint16_t audio_format = header[20] | (header[21] << 8);
    uint16_t num_channels = header[22] | (header[23] << 8);
    uint32_t sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint16_t bits_per_sample = header[34] | (header[35] << 8);

    if (audio_format != 1) return false;  // Must be PCM
    if (sample_rate != 48000) return false;  // Must be 48kHz
    if (bits_per_sample != 8 && bits_per_sample != 16 &&
        bits_per_sample != 24 && bits_per_sample != 32) return false;
    if (num_channels != 1 && num_channels != 2) return false;

    return true;
}
```

### Bit Depth Conversion (playback.h)

All bit depths convert to 32-bit for I2S:

```c
static void convert_samples_to_32bit(uint8_t *src, int32_t *dst, size_t sample_count, uint16_t bits_per_sample) {
    switch (bits_per_sample) {
        case 8:
            for (size_t i = 0; i < sample_count; i++) {
                // 8-bit is unsigned, center at 128
                dst[i] = ((int32_t)(src[i] - 128)) << 24;
            }
            break;
        case 16:
            for (size_t i = 0; i < sample_count; i++) {
                int16_t s = ((int16_t*)src)[i];
                dst[i] = ((int32_t)s) << 16;
            }
            break;
        case 24:
            for (size_t i = 0; i < sample_count; i++) {
                int32_t s = src[i*3] | (src[i*3+1] << 8) | (src[i*3+2] << 16);
                if (s & 0x800000) s |= 0xFF000000;  // Sign extend
                dst[i] = s << 8;
            }
            break;
        case 32:
            memcpy(dst, src, sample_count * 4);
            break;
    }
}
```

### Mono to Stereo Conversion (playback.h)

```c
// In playback_file_read_task, after reading from file:
if (playback_ctx.num_channels == 1) {
    // Mono file: duplicate each sample to both L and R channels
    int32_t *stereo_buf = (int32_t *)output_buf;
    // Process in reverse to avoid overwriting (in-place expansion)
    for (int i = samples_read - 1; i >= 0; i--) {
        stereo_buf[i * 2] = stereo_buf[i];      // Left
        stereo_buf[i * 2 + 1] = stereo_buf[i];  // Right (duplicate)
    }
}
```

### Playback Handler Update

```c
// In playback_play_handler:
cJSON *source_json = cJSON_GetObjectItem(root, "source");
const char *source = "recording";  // default
if (source_json && cJSON_IsString(source_json)) {
    source = source_json->valuestring;
}

char filepath[256];
if (strcmp(source, "backing-track") == 0) {
    snprintf(filepath, sizeof(filepath), "%s/%s", BACKING_TRACKS_FOLDER, filename);
} else {
    snprintf(filepath, sizeof(filepath), "%s/%s", SD_MOUNT_POINT, filename);
}
```

### Playback Validation Update

```c
// In playback.h, update validation to accept mono and various bit depths:
if (ctx->num_channels != 1 && ctx->num_channels != 2) {
    ESP_LOGE("PLAYBACK", "Only mono or stereo audio supported");
    return ESP_FAIL;
}
if (ctx->bits_per_sample != 8 && ctx->bits_per_sample != 16 &&
    ctx->bits_per_sample != 24 && ctx->bits_per_sample != 32) {
    ESP_LOGE("PLAYBACK", "Only 8/16/24/32-bit audio supported");
    return ESP_FAIL;
}
```
