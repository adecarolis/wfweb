# wfweb REST API

## Overview

The wfweb web server exposes a REST API over HTTP for scripting and integration with external tools. It covers the everyday radio controls, the station identity, the CW macros, the logbook and the SSTV gallery; a few controls (band stacking registers, scope settings, break-in, COMP level, TBW, memory writes) exist only on the WebSocket the page uses.

**Format:** JSON (`Content-Type: application/json`)
**Authentication:** None by default. With `--shared` (Shared Station, see
`SHARED_STATION.md`) every route but `/api/v1/auth/*` needs a session cookie
or an API token (`Authorization: Bearer <token>`), else `401`. Where the
admin admits visitors (`Shared/Anonymous=listen` or `control`), a request
without either is answered at the visitor's level instead.
**With `--shared` the radio is read-only over REST**: every `GET` below
works, every `PUT` / `POST` / `DELETE` under `/api/v1/radio/` answers `403`
to every account. See *Shared Station* below.
**CORS:** `Access-Control-Allow-Origin: *` on all responses (no credentials
are ever accepted cross-origin: the cookie is `SameSite=Strict`)

## Ports

wfweb runs two HTTP listeners simultaneously when SSL is available:

| Port | Protocol | Best for |
|------|----------|----------|
| 8080 | HTTPS (self-signed) | Browser — required for mic/audio (`isSecureContext`) |
| 8081 | HTTP (plain) | Scripts, `curl`, microcontrollers, home automation |

**Base URL (plain HTTP):** `http://<host>:8081/api/v1/radio`
**Base URL (HTTPS):** `https://<host>:8080/api/v1/radio` (add `-k` to curl for self-signed cert)

When SSL is not available on the host system, port 8080 is plain HTTP and port 8081 is the WebSocket port (no separate REST HTTP server in that case — use port 8080).

> **Microcontrollers (ESP32, Pico W, Arduino):** use port 8081 with plain HTTP. No TLS stack needed.
> With `--shared`, send an API token as `Authorization: Bearer <token>` on every request
> (made once from a logged-in browser: `POST /api/v1/auth/tokens`). Logins themselves are
> refused on this port — a password must not travel in clear.

> **Hamlib rigctld** (server build only): TCP, default port 4532, bound to `127.0.0.1`. Disabled by default — enable with `--rigctld-port 4532` or `RigCtldEnabled=true` in settings. Pass `--rigctld-bind-all` to listen on all interfaces (the Hamlib protocol is unauthenticated, so binding to a network interface hands PTT to anyone on the LAN). PTT requests route through the same path the WebSocket `setPTT` command uses, so RADE EOO synthesis and packet TX coordination stay intact. Use any Hamlib client (`rigctl`, fldigi, WSJT-X, POTACAT, gpredict) with `-m 2 -r 127.0.0.1:4532`.

---

## Response Format

All responses are JSON objects.

**Success (GET):** `200 OK` with requested data fields.

**Success (write to the radio):** `202 Accepted` — the command was queued. Writes are fire-and-forget via the cachingQueue; the radio will process them asynchronously.

```json
{"status": "accepted"}
```

Writes to the server's own state answer with the result instead: `200` with
the object for the station identity, the CW macros, a changed QSO and the
SSTV gallery, `201` for a new API token, account or reservation, and the
logbook's `POST` / `DELETE` answer `202` with the QSO or `{"status":
"cleared"}` / `{"status": "deleted"}`.

**Error:** appropriate 4xx/5xx status code with:

```json
{"error": "description"}
```

**Status codes:**

| Code | Meaning |
|------|---------|
| `200` | OK — GET success |
| `201` | Created — a new API token, account or reservation |
| `202` | Accepted — PUT/POST/DELETE queued |
| `400` | Bad Request — malformed JSON, a missing required field, or (`--shared`) a value of the wrong type |
| `401` | Unauthorized — `--shared` is on and the request carries no session cookie or API token (answered from the headers, before the body is taken); a wrong user name or password |
| `403` | Forbidden — `--shared`: not allowed for this account (`{"need": level}`); a write to the radio; an account route or a password change asked with a token instead of a logged-in browser; a wrong setup code or current password; a disabled account; a login over plain HTTP; a write with the session cookie from another site's page |
| `404` | Not Found — unknown API path |
| `405` | Method Not Allowed |
| `409` | Conflict — `--shared`: another account has the seat and the request logs a QSO or writes to the gallery (`{"error", "need": "seat"}`); a reservation overlaps another; a user name is taken; the change would leave the station without an admin; the target is the caller's own account or connection |
| `413` | Payload Too Large — a request over 16 MB (an SSTV picture over 4 MB). With `--shared` a body is capped by its route: 64 KB, except the ADIF import (16 MB), the rest of `/logbook` (4 MB), an SSTV picture (6 MB) and a user-list import (2 MB) |
| `429` | Too Many Requests — login lockout, or five wrong current passwords on a password change (`Retry-After` says for how long) |
| `500` | Internal Server Error — the logbook or the gallery could not be written |
| `503` | Service Unavailable — rig not connected; the SSTV directory unavailable; the server has too much unsent data queued (`Retry-After: 5`); or (`--shared`) the line of password checks is full (`Retry-After: 3`) |

A request's headers must be complete within 10 s of connecting, and a body
that stops arriving for 30 s is dropped; a slow upload that keeps moving is
waited for.

---

## Endpoints

### Authentication (Shared Station)

Only meaningful with `--shared`; without it `GET /api/v1/auth/me` answers
`{"shared": false}` and the other routes `404`. Full description, the account
management routes under `/api/v1/shared/users` and the deployment notes are in
`SHARED_STATION.md`.

| Route | Does |
|-------|------|
| `GET /api/v1/auth/me` | who the caller is: `{shared, authenticated, user:{id,name,callsign,onAir,role}, setupRequired, recovery, secure, anonymousAccess}` (`anonymousAccess`: what a visitor without a login gets, `none`, `listen` or `control`) |
| `POST /api/v1/auth/setup` `{code, username, password, callsign}` | HTTPS only; first run: the setup code from the console creates the admin and logs them in |
| `POST /api/v1/auth/login` `{username, password}` | HTTPS only; sets the `wfweb_session` cookie (`HttpOnly; SameSite=Strict; Secure`) |
| `POST /api/v1/auth/logout` | ends the session, clears the cookie, closes its WebSockets |
| `POST /api/v1/auth/password` `{current, password}` | HTTPS only, from a logged-in browser (not a token); changes the caller's password and ends the account's other sessions |
| `GET` / `POST {name}` / `DELETE /<id>` `/api/v1/auth/tokens` | the caller's API tokens, from a logged-in browser; the token value is returned once, on creation |

A browser without a session gets `{"type":"authRequired","setupRequired":bool}`
on the WebSocket and close code `4401` (unless the admin admits visitors);
with one, the first message is `{"type":"access","shared":true,"user":{...},
"role":...,"can":{...},"denied":{...},"policy":{...}}`.

With `--shared` every route needs the account's level: a `GET` is `listen`
(the accounts, their export, the policy and the connection list under
`/api/v1/shared/` are `admin` to read as well); writes to the SSTV gallery
are `control`, to the logbook `operate`; `/station`, `/cw/macros` and writes under
`/api/v1/shared/*` are `admin`. Short of it the answer is
`403 {"error":"needs operate access","need":"operate"}`. A path is the same
route with or without a trailing slash.
**An API token is for reading the station and for logging.** Whatever its
account's level it does not set the radio (below), and it does not manage
accounts (`/api/v1/shared/users*`), change a password or make another
token: those answer `403` with "from a logged-in browser". An admin's
token does read and set the policy (`/api/v1/shared/policy`), end a
connection and clear the seat. `info.logbookPath` (the server's own file
path) is given to an admin only.
**A shared station is not operated over REST.** Every write under
`/api/v1/radio/` (frequency, mode, gains, `/radio/tx`, `/radio/ptt`,
`/radio/cw`, a memory recall) answers
`403 {"error":"a shared station is operated from the page: over REST the radio is read-only"}`
to every account, the admin's included, with a token or a cookie. The
radio is tuned and keyed from the page, by whoever has the seat; a script
reads it. Local programs that must set the radio use rigctld.
While another account's browser holds the seat, logging a QSO or writing
to the gallery answers `409 {"error":"W1ABC has the seat","need":"seat"}`
(changing or deleting one's own QSO, and the export
stamp, are not held to the seat). Clearing the logbook and importing into it
are `admin`.
Policy, connections, the seat and the reservations (`/api/v1/shared/policy`,
`/api/v1/shared/connections`, `/api/v1/shared/seat`,
`/api/v1/shared/reservations`) are described in `SHARED_STATION.md`.

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
| `GET` | `/api/v1/station` | `{"callsign","grid"}`; with `--shared` also `onAir` (the call in use right now), `clubCallsign`, `onAirOwn`, `onAirOperator` |
| `PUT` | `/api/v1/station` | Set either or both: a field left out keeps the stored value, and so does an empty `grid`; an empty `callsign` clears it. Answers `200` with the new object |

Over the WebSocket the browser sends `setStationCallsign {callsign,grid}` and
every client receives `{"type":"stationChanged","callsign","grid"}`;
`rigInfo` carries `stationCallsign` and `stationGrid`.

### CW keyer macros

The ten macro keys of the CW bar are server state too, persisted under
`[CW]` (`Macro1` … `Macro10`; slot 5 is the page's EDIT key, which shows no macro) in
the settings file, so every browser shows the same set. `[CALL]` and `[QSO]`
in a macro are filled in by the browser when it is sent.

| Method | Endpoint | Action |
|---|---|---|
| `GET` | `/api/v1/cw/macros` | `{"macros": {"1": "CQ CQ DE [CALL] [CALL] K", "2": "..."}}` |
| `PUT` | `/api/v1/cw/macros` | Replace the whole set with the body's `macros` object (slots left out are cleared) |

Over the WebSocket the browser sends `setCWMacros {macros}` and every client
receives `{"type":"cwMacrosChanged","macros"}`; `rigInfo` carries `cwMacros`.

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
| `GET` | `/api/v1/logbook/{id}` | One entry |
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
optionally `comment`, `name`, `df`, `exported`, `stationCall`, `operator`.
Only `call` is required; `stationCall` (ADIF `STATION_CALLSIGN`) defaults to
the station callsign when a QSO is logged (imported files keep whatever they
carry); `operator` is ADIF `OPERATOR`. A record logged by an account also
carries `userId` (ADIF `APP_WFWEB_USER`, never taken from a client) and, in
the JSON only, `operatorName`, the account's name or `deleted account`.

**With `--shared`** (see "Callsigns and the log" in `SHARED_STATION.md`):

- *Stamping.* When an account logs a QSO the server sets `stationCall` to
  the call on the air at that moment (the account's own callsign while an
  `onAir: own` account holds the seat, else the station call), `operator`
  to the account's callsign and `userId` to the account, ignoring what the
  request sent for them. An edit keeps the stored record's three values.
- *Per-viewer filtering.* Admins get every record. An account gets the
  club log (records whose `stationCall` is the station callsign, or empty)
  plus the records it logged itself; a visitor without an account gets
  none. This holds for the page (`total` and `unexported` are the caller's
  own numbers), `GET /{id}` (`404` for a record the caller may not see),
  the full ADIF download (built from the visible records for anyone but an
  admin), `/worked` (for the call the caller transmits as: its own for an
  `own` account, else the station's; the reply names it in `callsign`) and
  the WebSocket deltas, which go only to sockets that may see the record.
- *Changes.* `PUT` / `DELETE /{id}` answer `403` unless the caller logged
  the record or is an admin; a record with no `userId` is admin-only.
- *Whole-log operations* (`DELETE /api/v1/logbook`, `POST
  /api/v1/logbook/adif`, the WebSocket `mergeLogbook`) are admin-only:
  `403 {"need":"admin"}` / `denied`.
- *Export.* `/export` and `/adif?new=1` give an admin the club log's new
  records and anyone else the new records they made under their own
  callsign (`?own=1` gives an admin theirs); `/exported` stamps only
  records the caller may export.

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

### SSTV image gallery

Pictures received in the browser's SSTV panel, and the pictures a user
loads to send, are uploaded to the server so every device sees the same
gallery. They live as `<id>.png` plus `<id>.json`
(the metadata) in `<data directory>/sstv` by default; set `SstvDir=` in the
settings file or pass `--sstv-dir <dir>` to choose another place (a relative
path in a named profile resolves next to that profile). Nothing is pruned:
pictures stay until deleted. An id is the UTC time of reception plus a
random suffix (`20261031T162328Z-ece7`) and is never reused.

| Method | Endpoint | Action |
|---|---|---|
| `GET` | `/api/v1/sstv/images` | `{"images":[...],"count":N,"dir":"...","persistent":bool}`, newest first |
| `POST` | `/api/v1/sstv/images?kind=&name=&mode=&freq=&call=&complete=&ts=&w=&h=` | Store a picture: the body is the PNG (`Content-Type: image/png`, 4 MB at most); reply `{"id","count"}` |
| `GET` | `/api/v1/sstv/images/{id}.png` | The picture (`Cache-Control: immutable`) |
| `DELETE` | `/api/v1/sstv/images/{id}` | Delete it; reply `{"count"}` |

A list entry carries `id`, `ts` (ISO 8601 UTC, from the upload's `ts` or
the server clock), `kind` (`rx`, received off the air, the default; or
`tx`, a picture loaded to send), `name` (the file name of a `tx` picture),
`mode` (`Robot 36`, `PD-120`, ...), `freqHz`, `call` (the receiving
station), `complete` (`false` when the signal ended before the last line),
`w`, `h` and `bytes`. A body that is not a PNG is refused
with 400, one over 4 MB with 413; an unknown or malformed id is 404.

Every store and delete, through REST or the web UI, is broadcast to all
connected browsers, which re-list the gallery:

```json
{"type":"sstvGallery","count":N}
```

`persistent` is `false` when the server runs in a container and the
directory is not on a mounted volume (the same check as the logbook).

```bash
curl -s -X POST 'http://localhost:8081/api/v1/sstv/images?mode=Robot%2036&freq=14230000&complete=1&w=320&h=240' \
  -H 'Content-Type: image/png' --data-binary @picture.png | jq .
```

---

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
    "version": "0.9.5",
    "logbookPath": "/home/alain/.local/share/wfweb/wfweb/logbook.adi",
    "logbookPersistent": true,
    "hasTransmit": true,
    "hasSpectrum": true,
    "modes": ["LSB", "USB", "AM", "FM", "CW", "CW-R", "RTTY", "RTTY-R"],
    "audioAvailable": true,
    "audioSampleRate": 48000,
    "txAudioAvailable": true,
    "preamps": [{"num": 1, "name": "Preamp 1"}, {"num": 2, "name": "Preamp 2"}],
    "hasAgc": true,
    "agcModes": [{"num": 1, "name": "FAST"}, {"num": 2, "name": "MID"}, {"num": 3, "name": "SLOW"}],
    "bands": [{"num": 20, "name": "160m", "start": 1800000, "end": 2000000}, {"num": 6, "name": "2m", "start": 144000000, "end": 148000000}],
    "filters": [{"num": 1, "name": "FIL1"}, {"num": 2, "name": "FIL2"}, {"num": 3, "name": "FIL3"}],
    "spans": [{"reg": 1, "name": "±2.5kHz", "freq": 5000}],
    "scopeModes": [{"num": 0, "name": "Center Mode"}, {"num": 1, "name": "Fixed Mode"}],
    "scopeFixedEdges": true,
    "txMeters": [{"kind": "swr", "cal": [[0, 1.0], [48, 1.5], [120, 3.0], [241, 6.0]], "red": 3.0}],
    "cwSpeedRange": {"min": 6, "max": 48},
    "manufacturer": "Icom"
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
    "attenuator": 0,
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

`version` is the release number (`0.9.5`) for a release build. A development
build appends its commit: `0.9.5+dev.e49e8c76` when built from a local tree,
`0.9.5+dev.20261115.e49e8c76` when built by GitHub (the id in the package's
file name).

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

The example shows the core fields. The object is the same one the web page
receives, so it carries more wherever the rig has the control, among them:
`agc` (see `/radio/rx`), `compressor` and `compLevel` (speech compressor on /
off and its level), `txBandwidth` (the SSB transmit filter, 0 WIDE / 1 MID /
2 NAR; SSB only), `breakIn` (0 off / 1 semi / 2 full), `cwPitch` (Hz),
`micGain` and `micInput` (`USB` or `LAN`: which of the rig's modulation
inputs the mic level belongs to), `alcMeter`, `monitor`, `antenna`,
`duplex` and the tone keys, `pbtInner` / `pbtOuter`, `scopeMode`, the
`freedv*` and reporter state. Likewise `info` says what the rig has:
`hasCompressor`, `hasCompLevel`, `hasTxBandwidth`, `hasBreakIn`,
`hasTuner`, `hasDuplex`, and each band's stacking register `bsr`.

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

`filter` is optional (1=FIL1, 2=FIL2, 3=FIL3). Omitting it selects FIL1.

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

The key takes the same path as the page's PTT and rigctld's: in RADE the
unkey sends the end-of-over frame first. With `--shared` this route only
reads: the `PUT` answers `403`.

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
{"afGain": 200, "rfGain": 255, "rfPower": 128, "squelch": 0, "micGain": 128}
```

### PUT /api/v1/radio/gains

All fields are optional. Only provided fields are updated.

| Field | Range | Description |
|-------|-------|-------------|
| `afGain` | 0–255 | Audio frequency gain |
| `rfGain` | 0–255 | RF input sensitivity |
| `rfPower` | 0–255 | TX power output |
| `squelch` | 0–255 | Squelch threshold |
| `micGain` | 0–255 | TX audio level: the rig's USB mod level, or its LAN mod level when the server reaches the rig over LAN |

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
| `agc` | int (0–255) | AGC setting: one of the `num` values in `agcModes` |
| `autoNotch` | bool | Auto Notch on/off |
| `filterWidth` | int (0–10000) | IF filter width in Hz |

`agc` is the rig's own value, which differs per rig: `agcModes` in
`/api/v1/radio/info` names them (IC-7300 `1` FAST / `2` MID / `3` SLOW,
IC-7600 `0`-`2` FAST-SLOW, IC-785x and IC-7200 with `0` = OFF, the Kenwoods
`0` OFF then SLOW upward). `hasAgc` is false, and `agcModes` absent, on a rig
whose rig file names no AGC settings. In FM the Icoms keep FAST and refuse a
change.

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
With `--shared` this route and its `DELETE` answer `403`.
Text longer than 30 characters is sent to an Icom rig as consecutive CI-V
frames of 30 (the rig keys them back to back); the browser spaces such
frames out so the keyer buffer never overflows, a script sending very long
text at once should do the same.

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

### GET /api/v1/radio/memories

The memory channels: `{"memories": [...], "count": n, "local": false}`. On a
rig whose memories cannot be read over CI-V (IC-718 and others) the list is
wfweb's own channels and `local` is `true`. Answers without a rig connected.

```bash
curl -s http://localhost:8081/api/v1/radio/memories | jq .
```

### POST /api/v1/radio/memories/{channel}/recall

Puts the radio on a memory channel. Optional body `{"group": n}` on rigs with
memory groups. `202 {"status": "accepted"}`, or `200 {"channel": n, "local":
true}` for one of wfweb's own channels; `400` for a channel that does not
exist.

```bash
curl -s -X POST http://localhost:8081/api/v1/radio/memories/5/recall | jq .
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
