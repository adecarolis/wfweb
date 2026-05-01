# wfview / wfweb Development Guide

## Project Overview
Headless Qt5 C++ app for controlling amateur radio transceivers (Icom, Kenwood, Yaesu).
The only user interface is web-based (browser). There is no desktop GUI.
Built from `wfweb.pro`.

---

## Build

```bash
# Linux - ALWAYS use wfweb.pro, NOT wfview.pro
qmake wfweb.pro && make -j$(nproc)

# Flags: -b (daemon), -l (logfile), -s (settings file)
```

See `BUILDING.md` for platform-specific prerequisites (Linux/Windows/macOS).

---

## Architecture

### Core Signal Chain
```
WebSocket commands (from browser)
    -> cachingQueue (deduplication, prioritization)
    -> rigCommander (command encoding, CI-V protocol)
    -> serial port (USB) or icomUdpHandler (LAN)
    -> radio
```

### Key Components
| Component | Role |
|-----------|------|
| `wfmain` | Main app logic, web server init |
| `cachingQueue` | Command queue with dedup and priority |
| `rigCommander` | CI-V command encoding/decoding |
| `webServer` | HTTP + WebSocket server (separate QThread) |
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
| `resources/web/` | C++ server build (`wfweb.pro`) | Server-only `index.html`, WebSocket transport, `packet.js`, `debug.html` |
| `resources/web-standalone/` | Static bundle (`tools/build-static.sh`) | Standalone `index.html`, `serial-transport.js`, `civ/`, `wasm/`, `packet.js`, `debug.html` |
| `resources/web-shared/` | Both | Pure assets only: CW decoder JS family, `ggmorse-wasm.js`, `models/`, `digits/`, `digits-sprite.png` |

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
| `src/wfmain.cpp` | Main app, web server init |
| `src/webserver.cpp` | Web server backend |
| `include/webserver.h` | Web server header |
| `include/prefs.h` | Preferences struct (contains `webPort`) |
| `resources/web/index.html` | Server-build SPA (WebSocket transport only) |
| `resources/web-standalone/index.html` | Standalone-build SPA (Web Serial only) |
| `resources/web-shared/` | Files shared by both builds (CW decoder, ggmorse, sprites, models) |
| `resources/web.qrc` | Qt resource file for the server build |
| `tools/build-static.sh` | Builds the standalone static bundle |
| `resources/ft8ts/dist/ft8ts.mjs` | FT8/FT4 decoder module |
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
- For synchronous cross-thread calls (e.g., EOO generation on PTT-off), use
  `BlockingQueuedConnection` with a shared member variable — do NOT use `Q_RETURN_ARG`

### RADE EOO Callsign Flow
- **TX**: `prepareTxEooBits()` encodes callsign via `rade_text_generate_tx_string()` on first TX frame.
  On PTT-off (`setPTT: false`), `generateEooAudio()` is called synchronously via `BlockingQueuedConnection`,
  the EOO audio is appended to `freedvTxBuffer`, and radio unkey is delayed 300ms so the frame plays out.
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
- One callsign in the gear dialog feeds CW, FT8/FT4, FreeDV Reporter, APRS, and
  the AX.25 link. Do not add per-mode callsigns — the shared one is intentional.

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
- The version is set in `wfweb.pro` (look for `DEFINES += WFWEB_VERSION=\"X.Y.Z\"`)
