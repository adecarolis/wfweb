# wfview REST API

## Overview

The wfview web server exposes a REST API over HTTP for scripting and integration with external tools. All radio control operations available via WebSocket are also available via REST.

**Format:** JSON (`Content-Type: application/json`)
**Authentication:** None (Phase 4)
**CORS:** `Access-Control-Allow-Origin: *` on all responses

## Ports

wfview runs two HTTP listeners simultaneously when SSL is available:

| Port | Protocol | Best for |
|------|----------|----------|
| 8080 | HTTPS (self-signed) | Browser — required for mic/audio (`isSecureContext`) |
| 8081 | HTTP (plain) | Scripts, `curl`, microcontrollers, home automation |

**Base URL (plain HTTP):** `http://<host>:8081/api/v1/radio`
**Base URL (HTTPS):** `https://<host>:8080/api/v1/radio` (add `-k` to curl for self-signed cert)

When SSL is not available on the host system, port 8080 is plain HTTP and port 8081 is the WebSocket port (no separate REST HTTP server in that case — use port 8080).

> **Microcontrollers (ESP32, Pico W, Arduino):** use port 8081 with plain HTTP. No TLS stack needed.

---

## Response Format

All responses are JSON objects.

**Success (GET):** `200 OK` with requested data fields.

**Success (write):** `202 Accepted` — the command was queued. Writes are fire-and-forget via the cachingQueue; the radio will process them asynchronously.

```json
{"status": "accepted"}
```

**Error:** appropriate 4xx/5xx status code with:

```json
{"error": "description"}
```

**Status codes:**

| Code | Meaning |
|------|---------|
| `200` | OK — GET success |
| `202` | Accepted — PUT/POST/DELETE queued |
| `400` | Bad Request — malformed JSON or missing required field |
| `404` | Not Found — unknown API path |
| `405` | Method Not Allowed |
| `503` | Service Unavailable — rig not connected |

---

## Endpoints

### GET /api/v1/radio

Full combined info + status.

```bash
curl -s http://localhost:8081/api/v1/radio | jq .
```

**Response:**
```json
{
  "info": {
    "connected": true,
    "model": "IC-7300",
    "version": "0.2.2",
    "hasTransmit": true,
    "hasSpectrum": true,
    "modes": ["LSB", "USB", "AM", "FM", "CW", "CW-R", "RTTY", "RTTY-R"],
    "audioAvailable": true,
    "audioSampleRate": 48000,
    "txAudioAvailable": true,
    "preamps": [{"num": 1, "name": "Preamp 1"}, {"num": 2, "name": "Preamp 2"}],
    "filters": [{"num": 1, "name": "FIL1"}, {"num": 2, "name": "FIL2"}, {"num": 3, "name": "FIL3"}],
    "spans": [{"reg": 1, "name": "±2.5kHz", "freq": 5000}]
  },
  "status": {
    "frequency": 14200000,
    "vfoAFrequency": 14200000,
    "vfoBFrequency": 7074000,
    "mode": "USB",
    "filter": 1,
    "transmitting": false,
    "sMeter": 30.0,
    "powerMeter": 0.0,
    "swrMeter": 1.0,
    "afGain": 200,
    "rfGain": 255,
    "rfPower": 128,
    "squelch": 0,
    "split": false,
    "tuner": 0,
    "preamp": 1,
    "autoNotch": false,
    "nb": false,
    "nr": false,
    "filterWidth": 0
  }
}
```

---

### GET /api/v1/radio/info

Rig capabilities and server info. Available even when rig is not connected (`connected: false`).

```bash
curl -s http://localhost:8081/api/v1/radio/info | jq .
```

**Response:** same as `info` object above.

---

### GET /api/v1/radio/status

All current radio state fields.

```bash
curl -s http://localhost:8081/api/v1/radio/status | jq .
```

**Response:** same as `status` object above. Returns `503` if rig not connected.

---

### GET /api/v1/radio/frequency

```bash
curl -s http://localhost:8081/api/v1/radio/frequency | jq .
```

**Response:**
```json
{"hz": 14200000, "mhz": 14.2}
```

### PUT /api/v1/radio/frequency

**Request body:**
```json
{"hz": 14200000}
```

```bash
curl -s -X PUT http://localhost:8081/api/v1/radio/frequency \
  -H 'Content-Type: application/json' \
  -d '{"hz": 14200000}' | jq .
```

---

### GET /api/v1/radio/mode

```bash
curl -s http://localhost:8081/api/v1/radio/mode | jq .
```

**Response:**
```json
{"mode": "USB", "filter": 1}
```

### PUT /api/v1/radio/mode

**Request body:**
```json
{"mode": "USB", "filter": 1}
```

`filter` is optional (1=FIL1, 2=FIL2, 3=FIL3). Omitting it keeps the current filter.

```bash
curl -s -X PUT http://localhost:8081/api/v1/radio/mode \
  -H 'Content-Type: application/json' \
  -d '{"mode": "CW", "filter": 2}' | jq .
```

Valid mode names come from the `modes` array in `/info` (radio-dependent).

---

### GET /api/v1/radio/vfo

```bash
curl -s http://localhost:8081/api/v1/radio/vfo | jq .
```

**Response:**
```json
{"vfoA": 14200000, "vfoB": 7074000}
```

### PUT /api/v1/radio/vfo

**Select active VFO:**
```json
{"active": "A"}
```
or `"B"`.

**Swap VFO A↔B frequencies:**
```json
{"action": "swap"}
```

**Equalize (copy active VFO to inactive):**
```json
{"action": "equalize"}
```

```bash
# Select VFO B
curl -s -X PUT http://localhost:8081/api/v1/radio/vfo \
  -H 'Content-Type: application/json' \
  -d '{"active": "B"}' | jq .

# Swap VFOs
curl -s -X PUT http://localhost:8081/api/v1/radio/vfo \
  -H 'Content-Type: application/json' \
  -d '{"action": "swap"}' | jq .
```

---

### GET /api/v1/radio/ptt

```bash
curl -s http://localhost:8081/api/v1/radio/ptt | jq .
```

**Response:**
```json
{"transmitting": false}
```

### PUT /api/v1/radio/ptt

**Request body:**
```json
{"transmitting": true}
```

```bash
# PTT on
curl -s -X PUT http://localhost:8081/api/v1/radio/ptt \
  -H 'Content-Type: application/json' \
  -d '{"transmitting": true}' | jq .

# PTT off
curl -s -X PUT http://localhost:8081/api/v1/radio/ptt \
  -H 'Content-Type: application/json' \
  -d '{"transmitting": false}' | jq .
```

---

### GET /api/v1/radio/meters

Read-only. S-meter, TX power, and SWR.

```bash
curl -s http://localhost:8081/api/v1/radio/meters | jq .
```

**Response:**
```json
{"sMeter": 54.0, "powerMeter": 0.0, "swrMeter": 1.0}
```

**Calibration notes:**
- `sMeter`: 0 = S9; each S-unit = 6 units (so S8 ≈ -6, S7 ≈ -12, etc.)
- `swrMeter`: ratio 1.0–6.0
- `powerMeter`: radio-dependent scaling

---

### GET /api/v1/radio/gains

```bash
curl -s http://localhost:8081/api/v1/radio/gains | jq .
```

**Response:**
```json
{"afGain": 200, "rfGain": 255, "rfPower": 128, "squelch": 0}
```

### PUT /api/v1/radio/gains

All fields are optional. Only provided fields are updated.

| Field | Range | Description |
|-------|-------|-------------|
| `afGain` | 0–255 | Audio frequency gain |
| `rfGain` | 0–255 | RF input sensitivity |
| `rfPower` | 0–255 | TX power output |
| `squelch` | 0–255 | Squelch threshold |

```bash
curl -s -X PUT http://localhost:8081/api/v1/radio/gains \
  -H 'Content-Type: application/json' \
  -d '{"afGain": 180, "rfPower": 100}' | jq .
```

---

### GET /api/v1/radio/rx

Receiver DSP settings.

```bash
curl -s http://localhost:8081/api/v1/radio/rx | jq .
```

**Response:**
```json
{
  "preamp": 1,
  "attenuator": 0,
  "nb": false,
  "nr": false,
  "agc": 2,
  "autoNotch": false,
  "filterWidth": 0
}
```

### PUT /api/v1/radio/rx

All fields optional. Only provided fields are updated.

| Field | Type | Description |
|-------|------|-------------|
| `preamp` | int (0–255) | Preamp selection |
| `attenuator` | int (0–255) | Attenuator level |
| `nb` | bool | Noise Blanker on/off |
| `nr` | bool | Noise Reduction on/off |
| `agc` | int (0–255) | AGC mode |
| `autoNotch` | bool | Auto Notch on/off |
| `filterWidth` | int (0–10000) | IF filter width in Hz |

```bash
curl -s -X PUT http://localhost:8081/api/v1/radio/rx \
  -H 'Content-Type: application/json' \
  -d '{"nb": true, "nr": true}' | jq .
```

---

### GET /api/v1/radio/tx

Transmitter settings.

```bash
curl -s http://localhost:8081/api/v1/radio/tx | jq .
```

**Response:**
```json
{"split": false, "tuner": 0, "compressor": false, "monitor": false}
```

`tuner`: 0=off, 1=on, 2=start-tuning.

> `compressor` and `monitor` may be absent if the rig has not reported them.

### PUT /api/v1/radio/tx

All fields optional.

| Field | Type | Description |
|-------|------|-------------|
| `split` | bool | Split mode on/off |
| `tuner` | int (0–2) | 0=off, 1=on, 2=start tuning |
| `compressor` | bool | Speech compressor on/off |
| `monitor` | bool | TX monitor (sidetone) on/off |

```bash
curl -s -X PUT http://localhost:8081/api/v1/radio/tx \
  -H 'Content-Type: application/json' \
  -d '{"split": true}' | jq .
```

---

### POST /api/v1/radio/cw

Send CW text.

**Request body:**
```json
{"text": "CQ DE K1AB K", "wpm": 20}
```

`wpm` is optional (range 6–48). If omitted, current radio speed is used.

```bash
curl -s -X POST http://localhost:8081/api/v1/radio/cw \
  -H 'Content-Type: application/json' \
  -d '{"text": "CQ DE K1AB K", "wpm": 20}' | jq .
```

### DELETE /api/v1/radio/cw

Stop CW transmission.

```bash
curl -s -X DELETE http://localhost:8081/api/v1/radio/cw | jq .
```

---

## CORS Preflight

All endpoints respond to `OPTIONS` with CORS headers, enabling use from browser JavaScript on any origin:

```bash
curl -s -X OPTIONS http://localhost:8081/api/v1/radio/frequency \
  -H 'Origin: http://example.com' \
  -H 'Access-Control-Request-Method: PUT' -v 2>&1 | grep -i "access-control"
```

---

## Shell Scripting Examples

```bash
# Monitor S-meter in a loop
while true; do
  curl -s http://localhost:8081/api/v1/radio/meters | jq '.sMeter'
  sleep 1
done

# QSY to 40m FT8
curl -s -X PUT http://localhost:8081/api/v1/radio/frequency \
  -H 'Content-Type: application/json' \
  -d '{"hz": 7074000}'
curl -s -X PUT http://localhost:8081/api/v1/radio/mode \
  -H 'Content-Type: application/json' \
  -d '{"mode": "USB-D"}'

# Check if transmitting
curl -s http://localhost:8081/api/v1/radio/ptt | jq '.transmitting'

# Reduce power to 50W (assuming 200 ≈ 100W for IC-7300)
curl -s -X PUT http://localhost:8081/api/v1/radio/gains \
  -H 'Content-Type: application/json' \
  -d '{"rfPower": 100}'
```
