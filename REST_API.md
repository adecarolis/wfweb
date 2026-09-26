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

> **Hamlib rigctld** (server build only): TCP, default port 4532, bound to `127.0.0.1`. Disabled by default — enable with `--rigctld-port 4532` or `RigCtldEnabled=true` in settings. Pass `--rigctld-bind-all` to listen on all interfaces (the Hamlib protocol is unauthenticated, so binding to a network interface hands PTT to anyone on the LAN). PTT requests route through the same path the WebSocket `setPTT` command uses, so RADE EOO synthesis and packet TX coordination stay intact. Use any Hamlib client (`rigctl`, fldigi, WSJT-X, POTACAT, gpredict) with `-m 2 -r 127.0.0.1:4532`.

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

### Station identity

One station callsign and grid for the whole server, persisted under
`[Station]` (`Callsign`, `Grid`) in the settings file. Every browser adopts
it on connect and any panel that changes it changes it for all of them; a
browser never overwrites it with a saved copy of its own (the old behaviour,
where the last browser to connect won, could silently swap the call on a
QSO). Logged QSOs are stamped with it (`stationCall`, ADIF
`STATION_CALLSIGN`), and it is the `myCall` of the remote-logging messages.

| Method | Endpoint | Action |
|---|---|---|
| `GET` | `/api/v1/station` | `{"callsign","grid"}` |
| `PUT` | `/api/v1/station` | Set either or both; an empty `grid` keeps the stored one |

Over the WebSocket the browser sends `setStationCallsign {callsign,grid}` and
every client receives `{"type":"stationChanged","callsign","grid"}`;
`rigInfo` carries `stationCallsign` and `stationGrid`.

### Server logbook

The server is the source of truth for the station logbook. It is a plain ADIF
file, `<data directory>/logbook.adi` by default; set `Logbook=` in the
settings file or pass `--logbook <file>` to choose another path (a relative
path in a named profile resolves next to that profile). Adds append one
record; edits and deletes rewrite the file atomically. On its first
connection a browser that still holds the old browser-local log merges it
into the server log once.

The log is never returned whole. `GET` pages newest-first with a cursor so a
lifetime log stays cheap for the browser and the server alike.

| Method | Endpoint | Action |
|---|---|---|
| `GET` | `/api/v1/logbook?limit=100&before=<cursor>&call=<CALL>` | Page of entries, newest first |
| `POST` | `/api/v1/logbook` | Add an entry from a JSON QSO object |
| `DELETE` | `/api/v1/logbook` | Clear the whole log (the previous file is kept as `logbook.adi.<timestamp>.bak`) |
| `PUT` | `/api/v1/logbook/{id}` | Replace an entry (the id is kept) |
| `DELETE` | `/api/v1/logbook/{id}` | Delete an entry |
| `GET` | `/api/v1/logbook/adif` | Download the complete ADIF file |
| `GET` | `/api/v1/logbook/worked` | Distinct callsigns with the bands each was worked on (`{"calls": {"K1ABC": ["20M", "40M"]}, "total": N}`); the browser's "new one" hints come from this |
| `GET` | `/api/v1/logbook/adif?new=1` | Only the QSOs not yet exported, as plain ADIF (no `APP_WFWEB_*` fields) |
| `GET` | `/api/v1/logbook/export` | `{"count","ids":[...],"adif":"..."}`: the same document plus the ids it contains |
| `POST` | `/api/v1/logbook/exported` | `{"ids":[...]}`: stamp those QSOs as exported |
| `POST` | `/api/v1/logbook/adif` | Import an ADIF document (body = the file); duplicates are skipped |

`GET /api/v1/logbook` returns `{ "entries": [...], "total": N, "next": "<cursor>" }`.
`limit` defaults to 100 (max 1000). Pass the returned `next` as `before` to
fetch the following, older page; it is absent on the last page. `call`
restricts the page to one callsign (case-insensitive), which is how a
"worked before" lookup is done against a large log. `total` is always the
size of the whole log.

A QSO object carries `date` (YYYYMMDD), `time` (HHMMSS), `call`, `freq` (Hz),
`band`, `mode`, `grid` (own), `theirGrid`, `rstSent`, `rstRcvd`, and
optionally `comment`, `name`, `df`, `exported`, `stationCall`. Only `call` is
required; `stationCall` defaults to the station callsign when a QSO is
logged (imported files keep whatever they carry).

**Export bookkeeping.** Each record carries an export stamp
(`APP_WFWEB_EXPORTED`, UTC `yyyyMMddTHHmmssZ`) once it has been included in
a "new QSOs" download; records without one are *new*, and every list and
event reports their number as `unexported`. The flow is two-step so that a
QSO logged while a download is in flight is never skipped: `GET
/api/v1/logbook/export` returns the new records as plain ADIF together with
their ids, the client saves the file, then `POST /api/v1/logbook/exported`
with those ids; the reply is `{"marked":n,"unexported":m}`. The full
download keeps wfweb's `APP_WFWEB_*` fields so it restores faithfully; the
new-QSOs document omits them, since it is meant for other services. `POST
/api/v1/logbook/adif` imports records as already exported (they come from a
log that handled its own uploads) unless called with `?new=1`; its reply is
`{"added","skipped","total","unexported"}`. Editing a record keeps its stamp.

Every change made through REST or the web UI is broadcast to all connected
browsers as a delta:

```json
{"type":"qsoAdded","qso":{...},"count":N}
{"type":"qsoUpdated","qso":{...}}
{"type":"qsoDeleted","id":"...","count":N}
{"type":"logbook","count":N,"unexported":M,"persistent":true}   // whole-log change: reload page 1
```

Browsers log through the WebSocket with `qsoLogged {qso}`, `updateQso {id,qso}`
and `deleteQso {id}`, and hand over a pre-server browser log with
`mergeLogbook {entries}`; the server applies the same validation and emits
the same deltas (each carrying `count` and `unexported`). `mergeLogbook` is
answered with `{"type":"logbookMerged","added":n,"total":N,"unexported":m,"persistent":bool}`.
Clearing the log is deliberately not offered in the web UI; use the REST
`DELETE`, which keeps the previous file as a `.bak`.

`persistent` (also in `rigInfo.logbookPersistent` and on every `logbook`
summary) is `false` when the server runs in a container and the logbook is
not on a mounted volume, i.e. the file is lost when the container is
recreated. Browsers then keep their own copy of what they log and re-send it
on every connect, and warn the operator in the log panel.

### Remote logging (WSJT-X UDP protocol)

Every logged QSO can be forwarded to an external logging program, so
GridTracker, JTAlert, Log4OM, CQRLOG, N1MM and similar pick it up without any
wfweb-specific code: wfweb speaks the WSJT-X UDP protocol they already
listen for. The server sends Heartbeat (every 15 s), Status, QSO Logged and
Logged ADIF (both, as WSJT-X does), optionally Decode, and Close at shutdown.
QSO messages fire from the server-side commit, so a manual SSB or CW entry
goes out the same way an FT8 contact does.

This is a server-side setting only: `[RemoteLog]` in the settings file
(`Enabled`, `Target`, `Decodes`) or `--remote-log <host[:port]>` on the
command line (port 2237 if omitted, implies enable), `--no-remote-log` (wins
over the settings file) and `--remote-log-decodes`. Where the station's QSO
stream goes is deployment configuration and the browser has no login, so the
web UI only shows the state (in the log panel's *Log management* menu; the
values come with `rigInfo` as `remoteLogEnabled`, `remoteLogTarget`,
`remoteLogDecodes`). Off by default. The target may be a multicast group such
as `239.255.0.0:2237`, which is what you need when more than one listener
runs on the same machine. The client id shown by listeners is `wfweb`, or
`wfweb - <name>` when `-n` is set. In Docker, use the host's LAN address for
unicast; multicast generally requires host networking.

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
    "name": "",
    "version": "0.2.4",
    "logbookPath": "/home/alain/.local/share/wfweb/wfweb/logbook.adi",
    "logbookPersistent": true,
    "hasTransmit": true,
    "hasSpectrum": true,
    "modes": ["LSB", "USB", "AM", "FM", "CW", "CW-R", "RTTY", "RTTY-R"],
    "audioAvailable": true,
    "audioSampleRate": 48000,
    "txAudioAvailable": true,
    "preamps": [{"num": 1, "name": "Preamp 1"}, {"num": 2, "name": "Preamp 2"}],
    "bands": [{"num": 20, "name": "160m", "start": 1800000, "end": 2000000}, {"num": 6, "name": "2m", "start": 144000000, "end": 148000000}],
    "filters": [{"num": 1, "name": "FIL1"}, {"num": 2, "name": "FIL2"}, {"num": 3, "name": "FIL3"}],
    "spans": [{"reg": 1, "name": "±2.5kHz", "freq": 5000}],
    "scopeModes": [{"num": 0, "name": "Center Mode"}, {"num": 1, "name": "Fixed Mode"}],
    "scopeFixedEdges": true,
    "txMeters": [{"kind": "swr", "cal": [[0, 1.0], [48, 1.5], [120, 3.0], [241, 6.0]], "red": 3.0}]
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

`frequency`, `mode` and `filter` are always present. They are `null` before
the first reply from the rig and whenever the rig has nothing to report - an
Icom sitting on a blank memory channel answers the frequency and mode reads
with `0xFF` instead of a value. Treat `null` as "unknown", never as 0 Hz.

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

Both fields are `null` when the rig has no frequency to report (blank memory
channel, or no reply yet).

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

Both fields are `null` when the rig has no mode to report (blank memory
channel, or no reply yet).

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

Read-only. S-meter, TX power, SWR, ALC, plus whichever of Comp/Vd/Id is
currently selected (see `setTxMeter` below).

```bash
curl -s http://localhost:8081/api/v1/radio/meters | jq .
```

**Response:**
```json
{"sMeter": 54.0, "powerMeter": 0.0, "swrMeter": 1.0, "alcMeter": 0.0}
```

**Calibration notes:**
- `sMeter`: 0 = S9; each S-unit = 6 units (so S8 ≈ -6, S7 ≈ -12, etc.)
- `swrMeter`: ratio 1.0–6.0
- `powerMeter`: radio-dependent scaling
- `compMeter` (dB), `vdMeter` (volts), `idMeter` (amps): present only while
  that meter is the selected one — the radio is polled for one at a time.

**Selecting the second meter:** the WebSocket command
`{"cmd":"setTxMeter","value":"swr"|"alc"|"comp"|"vd"|"id"}` chooses which
reading the browser's lower meter bar shows. `comp`, `vd` and `id` each start
a poll of their own and stop the previous one; `swr` and `alc` are always
polled, so selecting either just stops the extra poll. `info.txMeters` lists
the readings the connected radio can produce, each with the rig's own
calibration table (`[rigVal, reading]` pairs) and the reading at which its
meter face turns red.

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
{"split": false, "tuner": 0, "compressor": false, "monitor": false,
 "duplex": "OFF", "duplexOffset": 600000,
 "toneMode": "TSQL", "toneFreq": 1148, "tsqlFreq": 885,
 "dtcsCode": 23, "dtcsPolarity": 0}
```

`tuner`: 0=off, 1=on, 2=start-tuning.

`duplex`: repeater shift direction — `"OFF"`, `"DUP-"` or `"DUP+"`.
`duplexOffset`: the shift in Hz. Both are present only on rigs that support a
duplex offset (IC-705, IC-9700, IC-905, IC-785x).

`toneMode`: repeater access tone — `"OFF"`, `"TONE"`, `"TSQL"`, `"DTCS"`, or one
of the combined modes a rig with the Tone Squelch Type register can report
(`"DTCS(T)"`, `"TONE(T)/DTCS(R)"`, `"DTCS(T)/TSQL(R)"`, `"TONE(T)/TSQL(R)"`).
`toneFreq` / `tsqlFreq`: CTCSS tone in **tenths of a Hz** — 885 is 88.5 Hz.
`dtcsCode`: the DTCS code as printed on the radio (23 is D023).
`dtcsPolarity`: bitfield — bit 1 inverts TX, bit 0 inverts RX.
Only the keys the rig supports are present; `hasCTCSS` / `hasDTCS` /
`canSetToneFreq` / `canSetTsqlFreq` in `/api/v1/radio/info` say which.

> `compressor` and `monitor` may be absent if the rig has not reported them.

### PUT /api/v1/radio/tx

All fields optional.

| Field | Type | Description |
|-------|------|-------------|
| `split` | bool | Split mode on/off |
| `tuner` | int (0–2) | 0=off, 1=on, 2=start tuning |
| `compressor` | bool | Speech compressor on/off |
| `monitor` | bool | TX monitor (sidetone) on/off |
| `duplex` | string | Repeater shift: `"OFF"`, `"DUP-"`, `"DUP+"` |
| `duplexOffset` | int | Repeater shift in Hz (rounded down to 100 Hz) |
| `toneMode` | string | Access tone: `"OFF"`, `"TONE"`, `"TSQL"`, `"DTCS"` |
| `toneFreq` | int | CTCSS tone sent on transmit, in tenths of a Hz |
| `tsqlFreq` | int | CTCSS tone the squelch opens on, in tenths of a Hz |
| `dtcsCode` | int | DTCS code as printed on the radio (23 = D023) |
| `dtcsPolarity` | int | With `dtcsCode`: bit 1 inverts TX, bit 0 inverts RX |

A tone frequency the rig's own table doesn't contain is ignored rather than
rounded, so read `ctcssTones` / `dtcsCodes` from `/api/v1/radio/info` first.

```bash
curl -s -X PUT http://localhost:8081/api/v1/radio/tx \
  -H 'Content-Type: application/json' \
  -d '{"split": true}' | jq .
```

```bash
# 2 m repeater: 600 kHz down-shift with a 114.8 Hz access tone
curl -s -X PUT http://localhost:8081/api/v1/radio/tx \
  -H 'Content-Type: application/json' \
  -d '{"duplexOffset": 600000, "duplex": "DUP-",
       "toneMode": "TONE", "toneFreq": 1148}' | jq .
```

> Tone scan has no API at all: it runs in the browser, reading the repeater's
> sub-tone out of the received audio, so no command reaches the radio.

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

---

## WebSocket messages — Packet (Dire Wolf)

The packet modem is built into every wfweb binary. It decodes AX.25 / APRS
on the rig's RX audio with a single active demodulator at a time. Three
modes are selectable:

- `300`  — HF AFSK (mark 1600 / space 1800 Hz, 200 Hz shift)
- `1200` — VHF AFSK Bell 202 (mark 1200 / space 2200 Hz), standard APRS
- `9600` — VHF G3RUH scrambled baseband FSK

### Client → server

| Command | Payload | Effect |
|---------|---------|--------|
| `packetEnable` | `{"cmd":"packetEnable","value":true\|false}` | Master enable for the modem. Must be true for RX decoding or TX. |
| `packetSetMode` | `{"cmd":"packetSetMode","value":300\|1200\|9600}` | Select the active modem. Re-initializes the demodulator with the new parameters. |

### Server → client

| Message | Payload |
|---------|---------|
| `packetStatus` | `{"type":"packetStatus","enabled":bool,"mode":300\|1200\|9600}` — broadcast after `packetEnable` or `packetSetMode`. |
| `packetRxFrame` | `{"type":"packetRxFrame","chan":0,"ts":ms,"src":"CALL-N","dst":"DEST","path":["DIGI1","DIGI2*"],"info":"...","alevel":int}` — one per decoded AX.25 frame. `chan` is always `0` in v1. |
