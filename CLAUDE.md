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

# Server flags: -b (daemon), -l (logfile), -s (settings file)

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
| `resources/web/` | C++ server build (`wfweb.pro`) | Server-only `index.html`, `transport/websocket-transport.js`, `debug.html` |
| `resources/web-standalone/` | Static bundle (`tools/build-static.sh`) | Standalone `index.html`, `transport/serial-transport.js`, `civ/`, `wasm/` |
| `resources/web-shared/` | Both | `index.html`-side modules and pure assets: `theme.css` (design tokens + `.wf-*` kit), `packet.js`, `transport/rig-transport.js` (base class), CW decoder JS family, `ggmorse-wasm.js`, JS8 family (`js8.mjs`, `js8-panel.mjs`, `js8-panel.css`), `models/`, `digits/`, `digits-sprite.png` |

Build inputs for `ggmorse-wasm.js` (the `.cpp` source + license) live in
`resources/ggmorse-src/`, alongside `resources/build-ggmorse-wasm.sh`. They
aren't browser assets, so they're kept out of the SPA dirs entirely.

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
4. `tools/build-static.sh` already copies `web-shared/`, so the standalone build picks it up automatically

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
| `resources/web-shared/js8-panel.mjs` | JS8 messenger panel — markup + state machine, QSO tabs, RX/TX, CMD palette (both builds) |
| `resources/web-shared/js8.mjs` | JS8 WASM codec bridge — 8-FSK synth + encode/decode wrapper (both builds) |
| `resources/web-shared/` | Files shared by both builds (CW decoder, ggmorse, sprites, models) |
| `resources/web.qrc` | Qt resource file for the server build |
| `tools/build-static.sh` | Builds the Standalone static bundle into `dist/` |
| `tools/serve-static.py` | Local HTTP server for `dist/` with `Cache-Control: no-store` |
| `tools/fingerprint-static.py` | Cache-busts local asset URLs with `?v=<sha>` |
| `tools/extract-rig-caps.py` | Generates `civ/rig-caps.js` from `rigs/*.rig` |
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
