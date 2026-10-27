# wfview / wfweb Development Guide

## Project Overview
Headless app for controlling amateur radio transceivers (Icom, Kenwood, Yaesu).
The only user interface is web-based (browser). There is no desktop GUI.

There are two builds, both browser-driven:
- **Server** — Qt5 C++ binary built from `wfweb.pro`. Connects to the rig over
  USB or LAN and exposes HTTP + WebSocket. Required for LAN rigs, classic
  FreeDV, reporter spotting, and unattended deployments.
- **Standalone** — pure-browser SPA, no C++ in the loop. Drives a USB Icom
  directly via Web Serial. Built by `tools/build-static.sh` into a static
  `dist/` directory (published to https://wfweb.k1fm.us by
  `.github/workflows/pages.yml`).

Both builds share the SPA frontend (see "Frontend layout — three forks" below).

---

## Build

```bash
# Server build — Linux. ALWAYS use wfweb.pro, NOT wfview.pro
qmake wfweb.pro && make -j$(nproc)

# Server flags: -b (daemon), -l (logfile), -s (settings file), -n (top-bar name tag)

# Windows GUI launcher (separate exe, Qt Widgets) — build.bat and the Windows
# CI job build it after the server; standalone:  cd launcher && qmake && nmake release

# Standalone build — pure browser bundle, no C++ toolchain needed
tools/build-static.sh dist/
tools/serve-static.py              # local test on port 8000 (fixed), always serves <repo>/dist (no-store)
```

See `BUILDING.md` for platform-specific prerequisites (Linux/Windows/macOS) and
more on the Standalone bundle.

---

## Architecture

### Core Signal Chain
```
Server build:
  Browser (WebSocket commands)
      -> cachingQueue (deduplication, prioritization)
      -> rigCommander (command encoding, CI-V protocol)
      -> serial port (USB) or icomUdpHandler (LAN)
      -> radio

Standalone build:
  Browser
      -> SerialRigTransport (resources/web-standalone/transport/serial-transport.js)
      -> in-browser CI-V codec (resources/web-standalone/civ/icom.js)
      -> Web Serial port
      -> radio
```

### Key Components
| Component | Role |
|-----------|------|
| `servermain` | Main app logic, web server init |
| `cachingQueue` | Command queue with dedup and priority |
| `rigCommander` | CI-V command encoding/decoding |
| `webServer` | HTTP + WebSocket server (separate QThread) |
| `rigCtlD` | Hamlib rigctld TCP server (port 4532, lives on webThread) |
| `ConsoleStatus` | Terminal status page (URLs, rig, ports) shown instead of the log on an interactive TTY; main thread |
| `audioConverter` | Codec conversion (rig format <-> PCM) |
| `icomUdpHandler` | LAN UDP transport (3 channels) |
| `radeProcessor` | RADE V1 modem encode/decode (separate QThread) |
| `freedvProcessor` | Classic FreeDV codec (700D/700E/1600, separate QThread) |
| `freedvReporter` | Socket.IO client for FreeDV Reporter (qso.freedv.org) |
| `rade_text` | Self-contained RADE EOO callsign encoder/decoder (C) |
| `direwolfProcessor` | Vendored Direwolf modem (300/1200/9600 baud, AFSK + G3RUH FSK) |
| `ax25LinkProcessor` | AX.25 connected-mode link state machine (own dispatch thread for DLQ) |
| `aprsProcessor` | APRS frame parser, station db, beacon scheduler (lives in webserver thread) |

### Web Server
- HTTP/HTTPS on port 8080, WebSocket on 8081 (configurable via `WebServerPort` in prefs)
- Set port to 0 to disable; enabled by default
- Frontend: vanilla JS SPA, bundled via Qt resources (`resources/web.qrc`)
- Backend: `src/webserver.cpp`, `include/webserver.h`
- REST API documented in `REST_API.md`

### Frontend layout — three forks
The browser SPA lives in three sibling directories. **Each build pulls from a
specific subset; deleting or breaking a file in one fork cannot affect the
other.** No Direct/Server runtime gates: each `index.html` is single-purpose.

| Dir | Used by | Contents |
|-----|---------|----------|
| `resources/web/` | C++ server build (`wfweb.pro`) | Server-only `index.html`, `transport/websocket-transport.js` |
| `resources/web-standalone/` | Static bundle (`tools/build-static.sh`) | Standalone `index.html`, `transport/serial-transport.js`, `civ/`, `wasm/` |
| `resources/web-shared/` | Both | `index.html`-side modules and pure assets: `favicon.svg`, `theme.css` (design tokens + `.wf-*` kit), `packet.js`, `recorder.js` (+ vendored `lamejs/`), `needle-meter.js`, `rx-jitter.js`, `cw-keyer.js`, `cw-paddle.js`, `midi.js`, `paddle-panel.js`, `transport/rig-transport.js` (base class), CW decoder JS family (+ `ort/` ONNX Runtime Web and `models/deepcw.onnx`), JS8 family (`js8.mjs`, `js8-panel.mjs`, `js8-panel.css`), `models/`, `digits/`, `digits-sprite.png` |

The CW decoder is the DeepCW neural model (`resources/web-shared/models/deepcw.onnx`
+ `.json` metadata, AGPL-3.0, from e04/deepcw-engine) run by ONNX Runtime Web
(`resources/web-shared/ort/`, vendored from the `onnxruntime-web` npm package,
no CDN) inside `cw-decoder-worker.js`, an ES module worker. The model is a
window CTC decoder with context on both sides, not a streaming one: the
worker keeps a 12 s sliding window, decodes all of it every tick (~50 ms per
second of audio single-threaded on a laptop) and puts every label on an
absolute sample timeline. Labels younger than 3 s are the pending text (dim,
redrawn every tick, free to correct itself); a word older than that which the
model closed with a space, or followed with 1.5 s of silence, is committed
once by position and never revisited. **Never cut the audio on an energy
guess** — the first version did, and QSB fades read as key-up gaps, so the
windows started mid-character and whole words came out wrong (2026-10-22).
Both files are fetched lazily on the first DECODE press (~27 MB, cached by
the browser). `tests/cw_decoder_sim.mjs` (via `tests/test_cw_decoder.py`)
runs the real worker under node on synthesized CW at clean / -6 dB / -10 dB
and checks the committed text; **ggmorse stays as the fallback engine** (`cw-decoder-worker-ggmorse.js`,
a classic worker that upsamples the 3200 Hz tap to the 4000 Hz ggmorse needs):
the page probes WebAssembly SIMD before downloading anything (ONNX Runtime
ships SIMD builds only — Safari < 16.4, Firefox < 114, Chromium < 91 have
none), falls back when the DeepCW worker fails or times out loading, and when
`SLOW_TICKS` passes average more than `slowTickMs` (2.5 s; a Pi-class browser).
`localStorage wfCwDecoder` = `deepcw` | `ggmorse` pins an engine
(`CWDecoder.preferEngine()`); a GGMORSE tag on the text line says the fallback
is in use. Server side, `/models/` and `/ort/` are the only resources not sent
`no-store`: they carry a version+size ETag and answer 304 to `If-None-Match`
(`serveStaticFile`), otherwise every CW page load would fetch 27 MB again.
That is not enough on its own: Chrome keeps nothing in its HTTP cache for an
origin whose certificate it was told to accept -- any wfweb server on its
self-signed certificate re-downloaded all 27 MB on every DECODE press
(2026-10-25). So the worker keeps both files in Cache Storage
(`wfweb-cw-model`, `cachedFetch`) with the ETag they came with and asks with
`If-None-Match` on each load: 304 = use the stored copy, 200 = replace it,
unreachable server = the stored copy.
`COMPARE_GGMORSE` in `cw-decoder.js` is off (91e673f7): set to true, it also
runs ggmorse next to DeepCW and shows its copy on a second GGMORSE line for an
on-air comparison. Run `tests/test_cw_decoder.py` after any change to the
worker's streaming logic.

Build mechanics:
- **Server**: `web.qrc` aliases keep runtime URLs at `/web/...` regardless of
  whether the file lives under `web/` or `web-shared/`. C++ never sees the
  source-dir distinction.
- **Standalone**: `tools/build-static.sh` copies `web-standalone/` and then
  `web-shared/` into the same `dist/` layout (no collisions today; if there
  ever are, standalone wins because it's copied first).

When you want to share a file between both builds:
1. `git mv resources/web/<file> resources/web-shared/<file>`
2. Delete the duplicate from `resources/web-standalone/`
3. Update its `web.qrc` `<file alias=...>` line so the source path is `web-shared/<file>` (the alias stays the same — no C++ code changes)
4. Add it to the explicit file list in `tools/build-static.sh` — top-level `web-shared/` files are copied by name, not by glob (only its subdirectories are copied wholesale)

### Shared theme + component kit (`theme.css`)
`resources/web-shared/theme.css` is the single source of UI styling for both
builds (linked from each `index.html`, aliased in `web.qrc`, copied by
`build-static.sh` — same plumbing as `js8-panel.css`). Two layers:
- **Design tokens** (`:root`) — colors, type scale, spacing. Use these vars
  instead of hardcoded hex/px.
- **Component kit** (`.wf-*`) — one button / tab / field / progress shape for
  every mode panel. Each panel keeps its identity by setting `--mode-accent`
  via a `.mode-*` helper (`.mode-cw`, `.mode-digi`, `.mode-js8`, `.mode-packet`,
  …); the `.wf-*` components inherit that tint through `var()`.

Design intent is "one component kit, tinted per mode". Do **not** reintroduce
per-panel button/tab/input styling — set a `--mode-accent` and reuse `.wf-*`.

### BAND picker
The BAND overlay recalls the rig's band stacking register like the front-panel
key: the page sends `setBand {bsr, reg, save}` (CI-V `1A 01`), the transport
applies freq + mode + filter (`applyMemoryToVfo` on the server, the `1A 01`
branch of `serial-transport.js` on Standalone) and the normal readback
confirms. `bsr` comes per band in `rigInfo.bands[]` (rig-file `Bands\N\BSR`,
exported by `extract-rig-caps.py`; 0 = no register) and those bands fall back
to `setFrequency` with the page's `bandLastFreq`. Icom only: the Kenwood/Yaesu
rig files list band codes under the same "Band Stacking Reg" label, but their
commanders turn it into a native band-select (`BD`/`BU0`/`BS`) with no readable
record, so `addBandCaps` reports `bsr: 0` for them and the virtual rig. Data mode from the register is intentionally not applied
(wfweb owns data mode).

**The rig never stores the VFO into a register on CI-V tuning** — only its own
BAND key does (IC-7300 bench, 2026-10-09). So `save` names the register being
left (its `bsr` + the slot the page last recalled there, `bsrReg`) and the
transport stores the current VFO in it first, read-modify-write: a write
without the data/tone bytes gets NG, so the reply's bytes after the filter are
echoed back verbatim (`bandStackType.tail` / `parseBandStackReply().tail`),
which also keeps tones and sidesteps the per-rig layout of those bytes. On the
server, `queue->add()` echoes both the read and the write through
`receiveCache()` synchronously with the queue mutex held: the read echo has
freq 0, the write echo is flagged by `bsrWriteEchoPending` — touching the queue
on either deadlocks the server.

### Mode panels put the VFO back
FT8/FT4 and JS8 force USB and retune to their dial frequency on open. Both
`index.html` snapshot frequency + mode + filter first (`saveVfoSnapshot`) and
restore it on close (`restoreVfoSnapshot`) — skipped when the band was changed
inside the panel, and handed over instead of restored when one panel opens
straight over the other (`vfoSnapshotHandoff` in `closeOtherModes`). The
restore sends `setMode {value, filter}` as one command on purpose: a trailing
`setFilter` rebuilds from the server's mode cache before the mode reply is
back and would undo the mode. Packet and FreeDV do not use this.

### Speech compressor: COMP controls and the digital-TX hold (#147)
FUNC has a COMP tile (CI-V `16 44`, `setCompressor`, `compressor` in status /
update, gated on `hasCompressor`) and GAIN a COMP row (`14 0E`,
`setCompLevel`, `compLevel`, shown 0-10 like the radio, gated on
`hasCompLevel`). wfweb's digital modes transmit in plain USB / LSB, where
Icom keeps the voice COMP in the path (the older rigs force it off only in
the SSB data modes, IC-7600/7100/9100/785x manuals; the IC-7300-generation
manuals state no such rule, so even USB-D would not reliably take it out),
so every key wfweb modulates itself is flagged
`setPTT {value: true, digital: true}` (FT8/FT4, JS8, TUNE; FreeDV / RADE and
packet are known to the backend) and the backend switches COMP off before
the key and back on after the unkey: `holdCompressorForDigitalTx` /
`releaseCompressorHold` on the server (every unkey site calls the release,
including the vanished-client and shutdown paths), `_holdCompressor` /
`_releaseCompressor` in `serial-transport.js` (`_restoreCompressorDirect`
on close) and its mirror in `virtual-transport.js`. SSB only, never on an
unread state, never switched on if it was off, and a `setCompressor` during
the over wins. A new digital TX path must send `digital: true`.
Tests: `tests/test_compressor.py` (server, IC-7300 mock) and
`tests/comp_browser.mjs` (Standalone virtual rig).

### Scope click-to-tune step
A click, tap or drag on the scope lands on `scopeTuneStep()` (identical in
both `index.html` forks, used by `scopeFreqFromClientX`, the drag path and
`scopeFreqFromDragDeltaX`): 1 Hz in CW/CW-R, 100 Hz in every other mode. The
backends pass Hz through untouched (CI-V `05` is 1 Hz). The live click path
is the no-drag branch of `scopePointerEnd`; `scopeClick` is not wired. The
scope message carries its edges as float32 MHz, about 1 Hz at 14 MHz but
15 Hz at 144 MHz and 30 Hz at 432 MHz, which bounds a 1 Hz click up there.

### Touch operating controls (#144)
Phones and touch tablets only, gated on `(pointer: coarse)` in CSS and
`touchUi` in JS; the desktop keeps five equal soft keys and hold-to-talk.
Identical in both `index.html` forks (Web Serial has no mobile browser, but
Windows touch tablets run Standalone and the phone-layout bench drives the
Standalone virtual rig).
- **Key lock** (`#uiLockBtn`, `body.ui-locked`, `localStorage wfUiLock`): the
  HT-style lock in the scope's bottom-right corner. A tap locks, a ~0.8 s
  hold unlocks (the ring fills). A capture-phase guard on `document` swallows
  every pointer/touch/click/wheel outside `.ptt-btn`, the padlock, the
  AUDIO/MIC overlay (PTT opens it when the mic is off) and the start /
  power-off overlays, and nudges the padlock with a "hold to unlock" pill.
  Locking closes the open panel. `resizeCanvases()` lifts the padlock above
  the CW bar; the FT8/JS8/packet panels hide it while unlocked (`:has()`).
- **PTT row**: in portrait `#bottomBar` wraps, the four soft keys on top and
  PTT full-width underneath, 96 px tall and 16 px off the bottom edge (on
  Android that edge is the navigation gesture and the safe-area inset is 0,
  YO3GWM in #144); in landscape PTT is double width. Only where a
  mic PTT means something (`pttApplicable()`): voice modes with no digital
  panel open. CW keeps a plain KEY soft key; RTTY and the FT8/JS8/packet
  panels get the four soft keys alone (a `MutationObserver` on `#scopeArea`
  re-renders the bar as those panels open and close, and a latched TX is
  released when its key goes away). The desktop keeps PTT in every mode.
  The button carries a `.ptt-hint` sub-label and, latched, a `.ptt-timer`.
- **PTT latch**: hold PTT, slide the finger `PTT_LATCH_SLIDE_PX` up into
  `#pttLockCap` (a child of `#radio`, not of the bar, which
  `renderBottomBar()` rebuilds mid-press) and release there. `pttLatched`
  keeps the rig keyed; the button becomes "TX LOCKED · tap to stop". A tap
  can never latch. Ends on a tap, Escape / space, `visibilitychange` hidden
  or `pagehide` (immediate unkey, no 300 ms drain), the transport closing
  (`dropPttLatch()`), or `PTT_LATCH_MAX_MS` (3 min, `.warn` pulse for the
  last 15 s). Server side, `pttClient` remembers which browser keyed the
  rig and `onWsDisconnected` unkeys it if that client vanishes
  (`tests/test_ptt.py::test_ptt_released_when_keying_client_disconnects`,
  needs the `websockets` pip package).

### Meter faces
The top-bar meter (`#meterCanvas`) has two faces, picked in `drawMeter()`:
- **Bar** — the IC-7300 tile bar, inline in each `index.html` (`drawSMeter` /
  `drawTxMeters`): Po on top, a tap-to-cycle second row (SWR/ALC/COMP/Vd/Id).
- **Needle** — `resources/web-shared/needle-meter.js`, the IC-7610 screen
  meter. One pointer: S on RX, the selected reading on TX, so `po` joins the
  tap cycle there. The face is vector, from geometry measured off the rig's
  screen (scale arcs and needle have *different* centres); each scale is a
  `[reading → needle angle]` table, so the needle points at the printed value
  on any radio. Readings arrive ~5 Hz — the spring-damper in that file is what
  makes the motion continuous; it runs on `requestAnimationFrame` only while
  the needle is moving.

FUNC > METER cycles `AUTO / BAR / NEEDLE` (`localStorage wfMeterStyle`). AUTO
is the needle on the rigs in `NEEDLE_RIGS` (radios that draw this face
themselves), the bar elsewhere. `body.meter-needle` carries the layout: the
meter row of the `#radio` grid grows and the canvas takes the face's aspect.
The backends send the Power calibration table as `poCal` in `rigInfo` /
`rigConnected`; the page derives the rig's rated power from it for the Po %
scale of both faces.

### Standalone runtime — what replaces the C++ server
Without a server, the browser does in JS / WASM what the Server does in Qt:

| Server (C++)                          | Standalone (browser) |
|---------------------------------------|----------------------|
| `cachingQueue` + `rigCommander`       | dedup FIFO + CI-V codec inside `serial-transport.js` and `civ/icom.js` |
| serial port / `icomUdpHandler`        | `transport/serial-transport.js` (Web Serial) |
| Per-rig `.rig` files in `rigs/`       | `civ/rig-caps.js` (generated from `rigs/*.rig` by `tools/extract-rig-caps.py`) |
| `freedvProcessor` / `radeProcessor`   | `wasm/{rade,direwolf}.{mjs,wasm}` — committed in tree; rebuild only when WASM source changes via `tools/build-{rade,direwolf}-wasm.sh` (Emscripten) |
| `aprsProcessor` / `ax25LinkProcessor` | Browser-side AX.25 stack in `resources/web-shared/packet.js` driving the Direwolf WASM modem |
| `tools/virtualrig` (off-air loopback) | `resources/web-shared/airbus.js` + `transport/virtual-transport.js` (same-origin BroadcastChannel between two tabs at `/?virtual=1&id=N`) |
| FreeDV 700D/700E/1600                 | _Server-only_ (no codec2 WASM port) |
| PSK / FreeDV Reporter                 | _Server-only_ (disabled in Standalone UI) |

`.github/workflows/pages.yml` builds the Standalone bundle on every push and publishes it to wfweb.k1fm.us. `tools/fingerprint-static.py` rewrites every local asset URL with `?v=<sha>` so a redeploy actually busts the Pages cache.

### Audio Binary Protocol
| Direction | MsgType | Format |
|-----------|---------|--------|
| RX (rig->browser) | `0x02` | `[0x02][0x00][seq_u16LE][rateDiv_u16LE][PCM_Int16LE...]` |
| TX (browser->rig) | `0x03` | `[0x03][0x00][seq_u16LE][reserved_u16LE][PCM_Int16LE...]` |

### RX jitter buffer (`rx-jitter.js`)
RX frames go from `handleAudioData` into `WfJitterBuffer`
(`resources/web-shared/rx-jitter.js`), which runs inside the playback
AudioWorklet (its source is shipped there as text by `wfRxWorkletUrl()`, the
`recorder.js` pattern) or on the main thread for the ScriptProcessor fallback.
It sizes itself to the jitter it measures: an underrun fades out, re-buffers
and grows the target from the measured gap; every 2 s the stall budget the
period needed is recorded and the target shrinks back to the largest one seen
in the last `hold` periods (60 s, doubling on repeat underruns up to 10 min).
The depth is steered onto the target by resampling (+2 % / -0.4 %, which also
absorbs rig-vs-sound-card clock drift) and, above 250 ms excess, by a
crossfaded skip at a quiet moment; a backlog that large already present when
playback starts (frames queued before the AudioContext ran) is dropped at
once. It reports every 2 s (`onAudioJbEvent`): underruns and skips go to the
console as `[Audio] ...`, the AUDIO / TX overlay (FUNC > AUDIO, or the AUDIO
OFF warning) has a BUFFER tile (AUTO, or pinned to 50/100/200/400 ms,
`wfAudioBufLock`), and the learned size persists in `wfAudioBufMs`. `tests/rx_jitter_sim.mjs`
(via `tests/test_rx_jitter.py`) drives it under node through jitter, stalls,
drift and TX gaps — run it after any change to the class.

### Port Conventions
- 8080: HTTPS/web browser (with audio/mic support)
- 8081: HTTP REST API (scripts/microcontrollers)
- 50001-50003: UDP rig server mode

---

## Critical API Patterns (cachingQueue)

These patterns have caused bugs before. Follow them exactly:

```cpp
// ALWAYS use addUnique() for set commands (not add())
// add() creates duplicates; addUnique() deduplicates by command type
queue->addUnique(queuePriority::priorityImmediate, funcType, payload);

// Gain values MUST be QVariant::fromValue<ushort>(), NOT <uchar>()
queue->addUnique(pri, funcSetAfGain, QVariant::fromValue<ushort>(level));

// modeInfo.data MUST be explicitly set to 0 (no data mode)
// The default 0xff is INVALID and causes mode selection failures
modeInfo.data = 0;

// modeInfo.filter should default to 1 (FIL1)
modeInfo.filter = 1;

// A read never replaces a pending item of the same command: add() drops the
// new read and the pending set or read keeps its place. queueItem equality
// ignores the value, so addUnique(read) used to delete a queued set — the
// 5 Hz status read of the PTT state ate keys (#147) — and moved a pending
// read to the back of the line, so a value getCache() re-asked faster than
// the backlog drained was never read (COMP on the macOS CI runner).

// A read nobody answers is backed off like one the rig refuses: run() counts
// a read sent again with neither a value nor an NG since its last ask as a
// NAK (countUnanswered), three in a row and getCache() stops re-asking it for
// CACHE_NAK_RETRY_SECS (+ a per-command spread, so the retries do not all
// land on one status build). The test mocks ACK reads they do not implement — an
// FB to a read is no answer — and without this the ~30 of them stayed queued
// at immediate priority for the life of the server, putting every set 2 s
// behind (6 s+ on the macOS runner). Not FA in the mock: five FAs in a row
// read as "radio powered off" (consecutiveFAErrors) and polling stops.

// EXCEPTION: funcMemoryContents is addressed by channel, so two of them are
// never duplicates. Use add() — addUnique() compares command+receiver only
// and would evict a queued read/write for a *different* channel (#108).
queue->add(priorityImmediate, queueItem(funcMemoryContents, QVariant::fromValue<memoryType>(mem), false, 0));
```

---

## Key Files

| File | Purpose |
|------|---------|
| `wfweb.pro` | Qt project file |
| `src/main.cpp` | Process entry / CLI parser |
| `src/servermain.cpp` | Main app, web server + rigctld init |
| `include/servermain.h` | Main app header (contains `preferences` struct) |
| `src/webserver.cpp` | Web server backend |
| `include/webserver.h` | Web server header |
| `src/rigctld.cpp` | Hamlib rigctld TCP emulation (server build) |
| `include/rigctld.h` | rigctld header (signals: `pttRequested`) |
| `src/consolestatus.cpp` | Terminal status page: ANSI rendering, raw-mode keys, log ring buffer |
| `resources/web/index.html` | Server-build SPA (WebSocket transport only) |
| `resources/web-standalone/index.html` | Standalone-build SPA (Web Serial only) |
| `resources/web-standalone/civ/icom.js` | In-browser Icom CI-V codec (Standalone) |
| `resources/web-standalone/civ/rig-caps.js` | Generated per-rig caps for Standalone |
| `resources/web-standalone/transport/serial-transport.js` | Web Serial transport + dedup FIFO (Standalone) |
| `resources/web-standalone/transport/virtual-transport.js` | Browser virtual-rig transport (Standalone) |
| `resources/web-standalone/wasm/` | Committed RADE + Direwolf + JS8 WASM modems (used by both builds; standalone owns the dir, server aliases `wasm/js8.*` via web.qrc) |
| `resources/web-shared/transport/rig-transport.js` | Transport base class (both builds) |
| `resources/web-shared/airbus.js` | Browser-side BroadcastChannel "air" for Standalone virtual rigs |
| `resources/web-shared/packet.js` | Browser-side AX.25 / APRS / YAPP stack (both builds) |
| `resources/web-shared/recorder.js` | Audio (MP3) / video recorder behind the top-bar REC button (both builds) |
| `resources/web-shared/needle-meter.js` | IC-7610-style needle meter face + needle ballistics; alternative to the tile-bar meter in `index.html` (both builds) |
| `resources/web-shared/rx-jitter.js` | Adaptive RX jitter buffer (worklet + ScriptProcessor fallback, both builds); simulated by `tests/rx_jitter_sim.mjs` |
| `resources/web-shared/cw-keyer.js` | CW keyer model: timing, pacing of the text to the rig's keyer, browser sidetone (both builds); `tests/cw_keyer_sim.mjs` under node, `tests/cw_bench.mjs` on the two-rig bench, both via `tests/test_cw_keyer.py` |
| `resources/web-shared/cw-paddle.js` | Paddle / straight key as CW text entry: iambic A/B keyer, element decoder, adaptive straight-key decoder (both builds); `tests/cw_paddle_sim.mjs` |
| `resources/web-shared/paddle-panel.js` | Paddle settings (keyer A/B, swap, `wfPaddleOpts`), USB keyboard paddle adapter (VBand: left / right Ctrl), the PADDLE window of the CW bar (both builds); `tests/paddle_input_sim.mjs` |
| `resources/web-shared/midi.js` | Web MIDI controller: CTR2-MIDI presets, MIDI learn, knob-tick tuner, MIDI window body (both builds); `tests/midi_sim.mjs`, `tests/midi_browser.mjs` via `tests/test_midi.py` |
| `resources/web-shared/js8-panel.mjs` | JS8 messenger panel — markup + state machine, QSO tabs, RX/TX, CMD palette (both builds) |
| `resources/web-shared/js8.mjs` | JS8 WASM codec bridge — 8-FSK synth + encode/decode wrapper (both builds) |
| `resources/web-shared/` | Files shared by both builds (CW decoder + DeepCW model + ORT, sprites, models) |
| `resources/web.qrc` | Qt resource file for the server build |
| `tools/build-static.sh` | Builds the Standalone static bundle into `dist/` |
| `tools/serve-static.py` | Local HTTP server for `dist/` with `Cache-Control: no-store` |
| `tools/fingerprint-static.py` | Cache-busts local asset URLs with `?v=<sha>` |
| `tools/extract-rig-caps.py` | Generates `civ/rig-caps.js` from `rigs/*.rig` |
| `launcher/launcher.pro` | Windows GUI launcher (Qt Widgets), separate exe next to `wfweb.exe`; see "Windows launcher" below |
| `launcher/src/launchconfig.cpp` | Form fields → wfweb command line (pure Qt5Core, unit-tested) |
| `launcher/src/logparser.cpp` | Parses the server's `-l` log lines and classifies the ones the launcher reacts to (pure Qt5Core, unit-tested) |
| `launcher/src/serverprocess.cpp` | QProcess wrapper: `q` on stdin to stop, Windows Job Object, no console window |
| `tests/launcher_test.cpp` | Unit test for the two pure launcher helpers (`tests/test_cpp_units.py`, Linux CI) |
| `tools/make-launcher-icon.py` | Regenerates `launcher/res/wfweb.{png,ico}` from `resources/web-shared/favicon.svg` (resvg-py + Pillow; outputs are committed) |
| `tools/build-rade-wasm.sh` / `tools/build-direwolf-wasm.sh` / `tools/build-js8-wasm.sh` | Emscripten rebuilds of the browser modems (only when their C/C++ source changes) |
| `.github/workflows/pages.yml` | Publishes Standalone to wfweb.k1fm.us |
| `resources/ft8ts/dist/ft8ts.mjs` | FT8/FT4 decoder module |
| `resources/js8-src/` | Vendored JS8Call-improved subset — JS8 mode codec only, compiled to WASM (see `README-vendoring.md`) |
| `src/radeprocessor.cpp` | RADE V1 modem processing |
| `src/freedvprocessor.cpp` | Classic FreeDV codec processing |
| `src/freedvreporter.cpp` | FreeDV Reporter Socket.IO client |
| `src/rade_text.c` | RADE EOO text encoder/decoder (self-contained C) |
| `include/rade_text.h` | RADE text public API |
| `include/spotreporter.h` | Base class for reporter services |
| `src/direwolfprocessor.cpp` | DireWolf modem wrapper (TX/RX audio plumbing, self-test) |
| `src/ax25linkprocessor.cpp` | AX.25 connected-mode state machine wrapper |
| `src/aprsprocessor.cpp` | APRS parser, station db, beacon scheduler |
| `resources/direwolf/` | Vendored Direwolf subset (modem + AX.25 only — see `README-vendoring.md`) |
| `tests/test_packet.py` | End-to-end packet self-test (wraps `--packet-self-test`) |
| `CHANGELOG` | Release changelog |

---

## Terminal Status Page

- `ConsoleStatus` (`src/consolestatus.cpp`) replaces the scrolling log with a
  status page **only when stdin and stdout are a TTY**. Pipes, systemd, `-b`,
  `TERM=dumb`, `--no-tui` and `WFWEB_NO_TUI=1` (set in the Docker image) keep
  the plain log, byte for byte. `l` toggles page / live log, `q` quits.
- Plain ANSI escapes, no curses — it has to build on MSVC. The Windows console
  branch (`SetConsoleMode`) is compiled only by CI.
- **Never write to stdout directly** (`std::cout`, `printf`) from server code:
  it lands on top of the page. Use `qInfo`/`qWarning`; `messageHandler` in
  `main.cpp` routes the line to the log file and to `ConsoleStatus::logLine`.
- The class has **no `Q_OBJECT`** on purpose: `tests/consolestatus_test.cpp`
  links it against Qt5Core alone. `render()` is pure — layout changes are
  tested there; `tests/test_console.py` drives the real binary on a pty.
- `servermain::consoleSnapshot()` runs on the main thread. It reads the cache
  map directly, **not** `getCache()`, which re-requests stale values from the
  rig — the page must not generate rig traffic. Web-server state crosses from
  the web thread through `std::atomic` members of `webServer`.
- `ConsoleStatus::restoreTerminal()` must stay async-signal-safe (`write` +
  `tcsetattr` only): it runs from the signal handlers and `atexit`.
- The POSIX SIGINT/SIGTERM handler in `main.cpp` only restores the terminal and
  writes a byte to a pipe; a `QSocketNotifier` does the real shutdown in the
  event loop. Do not log or call Qt from the handler — it deadlocked when the
  signal landed while the main thread was inside `malloc`.

---

## FreeDV / RADE Architecture

### Processing Threads
- `freedvProcessor` and `radeProcessor` each run in their own QThread
- Cross-thread communication uses Qt signal/slot (queued connections)
- For synchronous cross-thread fire-and-forget (e.g. PTT-off triggering an EOO),
  use `BlockingQueuedConnection` so the caller waits for the slot to start
  before returning — do NOT use `Q_RETURN_ARG`

### RADE EOO Callsign Flow
- **TX**: `prepareTxEooBits()` encodes callsign via `rade_text_generate_tx_string()` on first TX frame.
  On PTT-off (`setPTT: false`), webserver invokes `RadeProcessor::sendEoo()` via
  `BlockingQueuedConnection`. `sendEoo` calls `generateEooAudio()` and emits `txReady`,
  which is routed by `onRadeTxReady` through the same TX path as voice frames —
  USB-attached rigs (`freedvTxBuffer` → ALSA drain) and LAN-attached rigs
  (`emit sendToTxConverter` → audio converter → LAN UDP) both work. Radio unkey
  is delayed 300ms so the frame plays out.
- **RX**: `rade_rx()` detects EOO → `rade_text_rx()` decodes → `rxCallsign` signal → webserver → browser
- **Important**: The callsign arrives AFTER sync is lost (EOO is the last frame). Do not gate
  reporter or UI updates on `freedvSync`.

### FreeDV Reporter
- Socket.IO Engine.IO v4 client over QWebSocket → qso.freedv.org
- Connects ONLY when FreeDV/RADE mode is active; disconnects on mode switch to SSB
- Reporter settings (callsign, grid, enabled) are saved regardless of mode
- RADE mode reports as `"RADEV1"` to the reporter service

### Web UI Command Flow
- `enableMic` — mic on/off (ALSA setup/teardown, DATA MOD switch)
- `setPTT` — radio TX on/off (CI-V command). EOO generation happens here, NOT in enableMic
- These are separate commands: mic stays enabled across PTT cycles

### Web UI Indicator Bar
- FreeDV indicator spans use stable IDs (`freedvSyncEl`, `freedvSnrEl`, `freedvCallsign`, `freedvFoEl`)
- The meters fast-path updates individual spans by ID — do NOT use `nth-child` selectors
  (the callsign span shifts all positions when present/absent)

---

## Recording Architecture

- Entirely browser-side, both builds (`resources/web-shared/recorder.js`); the
  server is not involved and stores nothing.
- The host page feeds PCM at three points, identical in both `index.html`:
  `Recorder.feedRx` in `handleAudioData`, `Recorder.feedTx` in `sendTxAudio`
  (every 0x03 frame actually sent), `Recorder.feedTxPcm` on the 0x04 packet-TX
  tee. New audio paths must pass through one of these to be recorded.
- A mixer AudioWorklet in its own 48 kHz context builds one mono timeline.
  **TX replaces RX while TX audio is queued** — a rig with MONITOR on loops TX
  back into the RX stream, and summing would record it twice. Mic frames are
  only kept while `isTransmitting` (the mic streams whenever it is enabled).
- Audio → MP3 via vendored `lamejs/lame.min.js` in a Blob worker. Video → a
  composited 1280×720 canvas (header + `meterCanvas` / `spectrumCanvas` /
  `waterfallCanvas`) through `MediaRecorder`; MP4 where supported, else WebM.
- The video body switches by mode: FT8, JS8 and packet replace the scope (as
  their panels do on the page); CW and FreeDV add a panel under it. `cwView()`,
  `ft8View()`, `js8View()`, `packetView()` and `freedvView()` in `recorder.js`
  are the only places that read page state for this (same pattern as
  `js8-panel.mjs` reading host helpers), so renaming any of these breaks the
  video view, not the page:
  - CW: `currentMode`, `CWDecoder.state.{enabled,textBuffer}`, `cwCharacters`,
    `cwSpeed`, `#cwScopeCanvas`, `#cwFilterBand`, `#cwQsoInput`. The RX/TX
    conversation is built per recording from the rolling decode buffer and the
    keyer's character list; text decoded while keyed is dropped (sidetone).
  - FT8/FT4: `digiBarVisible`, `digiMode`, `getDigiSlotInfo()`, `digiTxActive`,
    `digiTxFreq`/`digiRxFreq`, `#digiStatus`, `#digiDxCall`, `#digiWfCanvas`,
    `#digiWfLabelCanvas`, the two marker divs, and the **rendered rows** of
    `#digiRxPanel` / `#digiDirectedPanel` — row kind comes from the row's CSS
    classes (`cq-row`, `directed`, `highlight`, `tx-row`, `digi-tx-pending`,
    `digi-logged-row`, `digi-slot-divider`, `worked`).
  - JS8: all from the panel's DOM, since its state is module-private —
    `body.js8-open`, `#js8SubmodeSel`, `#js8ProgressBar`, `#js8Clock`,
    `#js8Status`, `#js8BandSel`, `#js8TxFreq`, `#js8WfCanvas`,
    `#js8WfTxMarker`, `#js8Stations .js8-station`, and the rows of `#js8Feed`
    (`.js8-feed-row` with `from-me` / `to-me`, `.ts` `.from` `.to` `.msg`
    `.snr` `.js8-tx-badge`).
  - Packet: `Packet.state` — `visible`, `mode`, `activeTab`, `txBusy`,
    `txActiveUntilMs`, `frames`, `terminal.{activeSid,sessions,compose}`,
    `aprs.stations` — plus `#packetScopeCanvas`.
  - FreeDV/RADE: `freedvEnabled`, `freedvModeName`, `freedvSync`, `freedvSNR`,
    `freedvFreqOffset`, `radeRxCallsign`. The page keeps only the latest
    callsign, so the heard list and the one-minute timeline are built per
    recording; the callsign is logged when it appears, never gated on sync.
- Video text uses filled tiles and light, low-saturation colours: thin
  saturated outlines smear in the encoder's chroma subsampling.
- The header meter is a copy of the page's `meterCanvas`, so the video shows
  whichever face is active. `getState().meterNeedle` only changes the layout:
  the needle face (`needle-meter.js`) takes the full header height and the top
  strip starts to its right; the bar face sits in the row under the strip.
- The worker and worklet are built from Blob URLs, so the lamejs URL carries a
  hand-appended `?v=` (`fingerprint-static.py` does not rewrite runtime URLs).

---

## Packet (AX.25 / Direwolf) Architecture

### Components
- `DireWolfProcessor` — modem only (300 AFSK, 1200 AFSK, 9600 G3RUH). Wraps the
  vendored Direwolf subset under `resources/direwolf/`. Audio I/O, PTT, IGate,
  KISS, digipeater, FX.25/IL2P are intentionally NOT vendored — wfweb supplies
  its own audio bus and PTT path.
- `AX25LinkProcessor` — connected-mode link state machine. Owns its own dispatch
  thread that drains the data-link queue (DLQ) and services link timers. Slots are
  safe to call from any thread; signals are emitted from the dispatch thread, so
  receivers connect with auto/queued connections.
- `AprsProcessor` — UI-frame filter, position parser (uncompressed/compressed/MIC-E),
  in-memory station database, beacon scheduler. Lives in the webserver thread.
  Beacons keep firing with no browser attached — that's intentional.

### Direwolf process-global state — single instance only
- Direwolf's modem uses static state. `DireWolfProcessor::active()` returns the one
  currently-active instance; the C shims in `wfweb_direwolf_stubs.c` dispatch into it.
  Don't try to run two `DireWolfProcessor`s.
- `DireWolfProcessor::ensureDlqInitialized()` wraps `dlq_init()` in `std::call_once`.
  Calling `dlq_init()` twice re-runs `pthread_mutex_init` on already-initialized
  mutexes and corrupts the queue. Both the AX.25 link processor and the standalone
  self-test path need this called before any RX.

### TX coordination
- One TX at a time across packet / FreeDV / RADE. `packetTxBusy` is claimed at the
  start of a packet TX and released on completion; an active `freedvTxActive`
  blocks `packetTx` with `packetTxFailed`. The flag must be claimed BEFORE the
  audio is queued — otherwise a second `packetTx` arriving in the same event loop
  tick races through.
- `transmitFrame(monitor)` takes a TNC-style `SRC>DST[,PATH,...]:info` string for
  one-shot UI frames. `transmitFrameBytes(chan, prio, frame)` takes a fully-formed
  AX.25 frame and is the connected-mode path.

### YAPP file transfer
- Real YAPP framing per IW3FQG spec: control-byte-typed packets. Per-kind framing
  table and the HD→RF critical detail are in memory under `yapp_protocol.md` —
  consult it before touching `yappSendFile` / `yappSendRR` / `yappAbortSend`.

### Self-test
- `./wfweb --packet-self-test` runs the modem loopback (encode → demod → verify)
  for every supported baud. `tests/test_packet.py` wraps it for CI.
- Run this after any change to `direwolfprocessor.*`, the vendored subset under
  `resources/direwolf/`, or `wfweb_direwolf_stubs.c`. Compile-clean is not enough.

### CW keyer
- The model lives in `resources/web-shared/cw-keyer.js` (`WfCwKeyer`,
  `WfCwSidetone`, `CW_MORSE`, `cwTokenDuration`), one copy for both forks;
  each `index.html` only builds the two objects (`cwKeyer`, `cwSidetone`),
  renders the transcript (`cwCharacters` is the keyer's own array, mutated
  in place, recorder.js reads it) and wires the buttons. **Do not put timing,
  pacing or sidetone logic back into the pages.** Timing follows the Icom
  keyer: 1:3, one dot between elements, three between characters, seven
  between words (a space after a character adds four, the character gap
  being counted with the character); `PARIS ` takes exactly 60000 / WPM ms.
  A 10-dot word gap once put the transcript, the pacing and the sidetone
  three dots behind the radio on every word (#141).
- **The Icom keyer does not key 1200 / WPM.** Measured off the air on the
  IC-7300 (`tests/cw_rig_capture.mjs`, BK-IN OFF so nothing is radiated): every
  dot is an odd number of 1/6 ms ticks, up to 1.3 % off the textbook value
  and not monotonic in the setting (20 WPM = 60.17 ms, 30 WPM = 39.5 ms). The
  model keys that table (`CW_ICOM_DOT_TICKS`, `cwDotMs`) when
  `rigInfo.manufacturer` is Icom, 1200 / WPM otherwise; the virtual rig keys
  the same table (`kIcomDotTicks`), so the bench stays blind to a textbook
  model — re-run the capture on the real radio when the model's timing is the
  claim. The sidetone follows the model; a measured drift of 0 at every speed
  is the acceptance test (#141).
- The Icom keyer command (CI-V `17`) takes **at most 30 bytes per frame**; a
  longer frame is NG'd and nothing is keyed, and so is one that would
  overflow the keyer's finite buffer (#141). The keyer hands text over in
  frames of at most 20 bytes (a `^XX` prosign never split), each due when the
  rig is down to 6 units of what was queued before, in CHR mode as much as
  for a long macro: the rig never holds more than ~26 bytes and never runs
  dry. `icomCommander` and `civ/icom.js` still split a longer frame at 30
  bytes as a backstop. Characters the keyer cannot send (not in `CW_MORSE`)
  are dropped before they reach the model or the radio.
- Stop paths: `cwKeyer.stop()` (STOP, close: nothing keyed, nothing shown),
  `cwKeyer.cut()` (Escape, Backspace in CHR mode: cut the rig and the tone,
  drop what is pending, rewind the model clock to now so the next text starts
  at once). The characters already handed to the keyer cannot be taken back.
- Server build: the CW macros live in the settings file (`[CW]`
  `Macro1..Macro10`, `applyCwMacros`), pushed as `cwMacros` in `rigInfo` and
  `cwMacrosChanged`; the browser only migrates a pre-existing localStorage
  set to a server that has none. Standalone keeps them in localStorage.
- Two sidetones, one at a time. TONE in the CW bar (`WfCwSidetone`,
  `localStorage wfCwSidetone`, on by default) is keyed from the model's
  element times, one oscillator with a 5 ms envelope in an AudioContext of
  its own, parked only after the furthest scheduled element (a typed space
  schedules nothing); no settings of its own: pitch = the rig's CW PITCH
  (`cwPitch` in Hz in status / update messages: `funcCwPitch` on the server,
  the `0x14 0x09` read in `serial-transport.js`), level follows VOL. The
  rig's own sidetone (dffd32c9) comes back in the RX audio over USB only
  (IC-7300: ACC/USB AF Beep/Speech Output ON), never over LAN; it is the one
  that follows a paddle and gives true full break-in, with TONE off. The
  browser tone never enters the RX chain, so the CW decoder and the recorder
  do not see it.
- The tone's delay is the output device's (`baseLatency + outputLatency`):
  ~30 ms on Mac speakers, ~170 ms on AirPods, where a paddle cannot be sent
  by ear. An AudioContext keeps the latency of the device it opened on (168
  ms after the AirPods, while a fresh one on the speakers had 32 ms), so a
  change of output calls `cwSidetone.reset()`: a new context at once, which
  measures itself (deferred while keying); TONE off / on rebuilds it too.
  On a Mac the switch gives no `devicechange` and no device labels (no mic
  permission); the RX context's `outputLatency` follows it (168 -> 40 ms), so
  a jump of more than 20 ms there is the trigger, checked every second.
  `onLatency` reports the delay once a context plays; above
  `CW_TONE_SLOW_MS` (80) TONE turns amber (`.wf-btn.lag`) and says why.
- BK-IN in the CW bar header (between LN and TONE) is the radio's break-in key,
  CI-V `16 47` = `funcBreakIn`: OFF (the keyer sounds its sidetone, nothing is
  transmitted) -> BKIN (semi) -> F-BKIN (full), labels as on the radio's
  screen. `setBreakIn {value}`, `breakIn` in status / update, `hasBreakIn` in
  rigInfo (rig-file "Break-In Status"; Standalone: `hasBreakIn` in
  `civ/rig-caps.js`, read at connect and on the slow tick). The commander
  refuses a value above the rig file's `Max`, so FULL needs `Max=2` there
  (IC-7300 fixed in 2.12; several other Icom files still say 1).
- Tests: `tests/cw_keyer_sim.mjs` (node, virtual clock, fake AudioContext)
  checks timing, pacing, Backspace, unknown characters and the sidetone;
  `tests/cw_bench.mjs` runs on the two-rig bench and measures the model
  against the virtual rig's **actual keyed audio** (a bench that compares the
  tone with the page's own model cannot see a model error). The virtual rig
  emulates a 30-byte keyer buffer (`civEmulator::keyerPending`) and NGs an
  overflowing frame, so type-ahead bugs fail there. Run both after any change
  to the module (`pytest tests/test_cw_keyer.py`).

### MIDI controller and paddle (`midi.js`, `cw-paddle.js`)
- Browser-only, both builds, no server command of its own: the controller
  drives the actions each `index.html` registers (`midi.register(name,
  {kind, fn})` — `rel` knob ticks, `abs` 0..1 levels, `btn`, `gate`), which
  call the same functions as the page's own controls (`stepBand` /
  `recallBand`, `setMode`, `setCwSpeedValue`, the GAIN sliders driven by an
  `input` event, `setSpan`, `midiPtt` = the PTT button's path). FUNC > MIDI
  opens `#midiOverlay`, whose body `WfMidiPanel` builds. The tile shows
  only while a controller is plugged in (`midi.devices`, hot-plug
  re-renders FUNC). `midi.watch()` at load sees one without switching MIDI
  on, but only where `permissions.query({name: 'midi'})` is already
  `granted` — Chrome prompts for any MIDI access, and a prompt at every
  page load is worse than no tile. Until that first grant (and in Safari /
  iOS, plain http) the window is reached from CW bar > PADDLE > MIDI...,
  which also says why there is no MIDI. Off until switched on:
  `localStorage wfMidiEnabled`, the map in `wfMidiMap`, knob options in
  `wfMidiOpts`; switched off, MIDI still lists the controllers but acts on
  no message.
- The map is a table, `[{type: 'cc'|'note', num, enc, action}]`, with
  CTR2-MIDI presets and MIDI learn. CTR2 (manual v2.01, channel 1): knob CC
  100/101 Home turn / push-turn (WheelA, 64 ± ticks, speed-sensitive ±1/10/50),
  102-107 the other knob modes (SliderA 0..127 by default); buttons NoteOn on
  release, 1-6 short / 11-16 long; paddle jack 20/21, extended paddle mode
  30/31 (straight key / PTT). **Extended BTN mode** puts buttons on 1-48 and
  moves the jack to 96-99 — hence the second preset, not one fixed table.
- Knob ticks go through `WfMidiTuner` (one `setFrequency` per 40 ms, each
  step from the last frequency sent while the readback trails): never send
  one `setFrequency` per tick from the page.
- A gate (paddle, key, PTT) is released on unplug, on disable and on a preset
  load, so a controller pulled mid-TX cannot leave the rig keyed.
- **The paddle is text entry, not keying.** Icom CI-V has no element-level
  key-down, so the iambic keyer in `cw-paddle.js` (mode A / B, swap) is heard
  through `cwSidetone` at once, its elements are decoded (character after a
  2-dot gap, word after 5; a pattern that is no character is sent as its
  longest known head, one extra element dropped (`......` -> 5) and more
  decoded as further characters (`..-.--` -> F M), so a slip never leaves
  a hole) and
  each character goes to `cwKeyer.sendText(ch, {tone: false})` — the CHR-mode path — so the rig's
  keyer sends it, one character behind the fist. `tone: false` marks the
  units `noTone` so the text path never tones them a second time. Paddle input
  is ignored outside CW / CW-R; the first contact opens the CW bar.
  The radio's own sidetone (in the RX audio over USB) therefore trails the
  fist by a character (~0.9 s at 20 WPM on the bench): `WfCwRxMute` cuts the
  RX playback gain (`setRxPaddleMute`, `rxPlaybackGain()`) on the first
  element and restores it once the radio's keying (`cwKeyer` model end) plus
  the jitter buffer and 300 ms have passed, so only the browser tone is heard. Timing
  stays in `cw-paddle.js`, not in the pages (CW keyer rule).
- **PADDLE window** (`paddle-panel.js`, button at the end of the CW bar's
  CALL/QSO row; the CW header has no room left on a phone): the keyer
  options (iambic A / B, swap -- they moved here from the MIDI window,
  `wfMidiOpts` values carried over once into `wfPaddleOpts`) and the
  paddle's adapter. FUNC > MIDI stays the MIDI controller (knob, buttons,
  its paddle-jack notes). The **USB keyboard adapter** (VBand, Vail adapter
  in keyboard mode) is a keyboard whose paddle contacts are the left and
  right Ctrl keys (`KeyboardEvent.code` `ControlLeft` / `ControlRight`,
  LEARN for others; straight-key mode uses the left one). Keyboard events
  reach every browser, phones included, where Web MIDI does not. Opt-in
  (USB KEYS), because those are ordinary Ctrl keys: `WfKeyboardPaddle`
  listens on `window` in the capture phase, ignores key repeat, and while a
  contact is down in CW holds every other key back (no Ctrl+letter shortcut
  mid-character; the browser's own Ctrl+W / Ctrl+T on Windows and Linux
  cannot be held back by a page); a lost focus releases what is held. Every
  contact, MIDI or keyboard, goes through the page's `paddleContact()`, which
  lights the window's CHECK lamps in any mode and keys only in CW / CW-R.
- Tests: `tests/cw_paddle_sim.mjs` (decode at several speeds, A vs B squeeze,
  swap, jittery straight key, sidetone on the paddle, rig text one character
  behind), `tests/midi_sim.mjs` (encodings, presets, learn, hot-plug,
  tuner), `tests/paddle_input_sim.mjs` (keyboard adapter, settings, a CQ on
  the Ctrl keys), `tests/midi_browser.mjs` (Standalone on `?virtual=1` with a
  fake `requestMIDIAccess` and real key events; never a real radio).

### Shared station callsign
- One callsign in the gear dialog feeds CW, FT8/FT4, JS8, FreeDV Reporter, APRS, and
  the AX.25 link. Do not add per-mode callsigns — the shared one is intentional.

---

## JS8 Architecture

### Browser-side, both builds
- JS8 is entirely browser-side, like FT8 — there is **no C++ server component**.
  The codec is a WASM module (`resources/web-standalone/wasm/js8.{mjs,wasm}`,
  source vendored under `resources/js8-src/`, built by `tools/build-js8-wasm.sh`).
- The panel and codec bridge live in `resources/web-shared/` (`js8-panel.mjs`,
  `js8.mjs`, `js8-panel.css`), so both the server and standalone SPAs load them.
  The server build reaches `wasm/js8.*` through a `web.qrc` alias; the standalone
  build gets it via `tools/build-static.sh`'s `web-shared/` copy step.

### Codec bridge (`js8.mjs`)
- `js8Init()` → wasm module; `encode()` → 79 tones; `newDecoder()` → push samples,
  drain decodes; `synthesize(tones)` → Float32 @ 12 kHz continuous-phase 8-FSK
  (phase accumulator MUST stay continuous across symbols or sync fails).
- Submodes mirror `JS8_Include/commons.h` and JS8Call 3.0.0 labels:
  Slow (30 s) / Normal (15 s) / Fast (10 s) / JS8 40 (6 s) / JS8 60 (4 s).

### Panel (`js8-panel.mjs`)
- Owns the full panel: markup (template literal), RX decoder loop, TX queue, CMD
  palette, QSO tab manager, group/@ALLCALL channels, HB scheduler, auto-replies,
  relay forwarding, and ADIF logging to the shared log.
- It's an **overlay peer of the FT8 DIGI bar** — opening one closes the other via
  `closeOtherModes('js8')`.
- **RX**: taps `int16` chunks via `window._js8ProcessAudioChunk`, which the host's
  audio path calls. **TX**: routes through `streamDigiAudio(...)` — the same sink
  FT8 uses (WebSocket on server, Web Serial on standalone).
- Reads host helpers off the page: `send`, `streamDigiAudio`, `haltDigiTx`,
  `initDigiTxCtx`, `startDigiTune`/`stopDigiTune`, `wfBuf`/`drawWfCanvas`,
  `closeOtherModes`. Everything else stays local to the module.

---

## Windows launcher (`launcher/`)

`wfweb-launcher.exe` is a **separate** Qt Widgets exe shipped in the Windows
zip. The server stays CLI-only: nothing under `src/`, `include/` or
`wfweb.pro` is involved, and the launcher includes only `wfweb_version.h`.
It composes the same flags a user would type and supervises `wfweb.exe` as a
child. The contract it relies on, all verified against the server source:
- **Flags model.** Fields live in `QSettings("wfweb", "launcher")` and become
  CLI flags on every start (`LaunchConfig::toArguments`). The server never
  persists CLI overrides, so its own settings are untouched.
- **Stop = `q` on stdin.** The keyboard thread (`src/keyboard.cpp`) reads
  stdin even on a pipe; `q` runs the full teardown. Closing stdin does *not*
  quit; `kill()` skips teardown. The launcher writes `q`, waits 10 s, then kills.
- **Log via the `-l` file, not stdout.** stdout is block-buffered on a pipe;
  the log file is flushed per line (`messageHandler`, `src/main.cpp`), format
  `yyyy-MM-dd hh:mm:ss.zzz LVL category: msg`. stdout/stderr go to the null
  device. `LogParser::classify` matches exact server strings — if you reword a
  listening / serial / LAN log line, update the parser and `tests/launcher_test.cpp`.
- **Up via REST.** A bind failure does not exit the server, so readiness is
  `GET /api/v1/radio/info` on `webPort+1` (HTTPS mode) or `webPort` (plain
  HTTP fallback); whichever answers fixes the browser scheme.
- **Windows only, under `#ifdef Q_OS_WIN`:** `CREATE_NO_WINDOW` for the child
  and a Job Object with kill-on-close, so the server dies with the launcher.
- **Port conflicts** (`launcher/src/portowner.cpp`): the refusal dialog names the
  process listening on the web port (`GetExtendedTcpTable`, image name, command
  line read from its PEB) and offers to stop it only when it is `wfweb.exe`,
  with a warning for `--lan` instances. Never automatic, never for other programs.
- Built by `build.bat` and the Windows CI job only; the pure helpers are
  tested on Linux CI through `tests/test_cpp_units.py`. Dialog-showing paths
  must run inside the event loop (`--start` is deferred with a 0 ms timer).

## Workflow Rules

### 1. Plan First
- Enter plan mode for ANY non-trivial task (3+ steps or architectural decisions)
- If something goes sideways, STOP and re-plan immediately
- Write detailed specs upfront to reduce ambiguity

### 2. Subagent Strategy
- Use subagents liberally to keep main context window clean
- Offload research, exploration, and parallel analysis to subagents
- One task per subagent for focused execution

### 3. Self-Improvement Loop
- After ANY correction from the user: update `tasks/lessons.md` with the pattern
- Write rules for yourself that prevent the same mistake

### 4. Verification Before Done
- Never mark a task complete without proving it works
- Run the build to verify changes compile
- Ask: "Would a staff engineer approve this?"

### 5. Demand Elegance (Balanced)
- For non-trivial changes: pause and ask "is there a more elegant way?"
- Skip this for simple, obvious fixes - don't over-engineer

### 6. Autonomous Bug Fixing
- When given a bug report: just fix it. Don't ask for hand-holding
- Point at logs, errors, failing tests - then resolve them

---

## Core Principles
- **Simplicity First**: Make every change as simple as possible. Impact minimal code
- **No Laziness**: Find root causes. No temporary fixes. Senior developer standards

---

## Release Process
- When bumping the version, always update the CHANGELOG before committing
- Add a new section at the top of the CHANGELOG with the version and date
- Summarize commits since the last release into user-facing categories
- Check existing tags (`git tag -l`) to find the next available version number
- The version is set in `include/wfweb_version.h` (look for `#define WFWEB_VERSION "X.Y.Z"`).
  It moved out of `wfweb.pro` so Make's header-dependency tracking rebuilds every
  object that bakes in the version string (see the v0.7.1 reporter-version bug).
