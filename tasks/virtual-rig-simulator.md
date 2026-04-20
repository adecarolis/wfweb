# Virtual Rig Simulator — Implementation Plan

## Context

The user has one physical transceiver but wants to develop/test features that inherently require multiple radios communicating with each other — FreeDV/RADE codec work, mesh-network experiments, multi-station scenarios. Doing this end-to-end today requires a second radio, a second operator, and actual RF, which is impractical.

This plan introduces a **virtual rig simulator**: a standalone binary that presents itself to `wfweb` clients as N independent Icom radios over the LAN UDP protocol, and routes audio between them through a software "ether." Each `wfweb` instance connects to one virtual rig and behaves exactly as if it were talking to real hardware. When any rig keys PTT, its TX audio is mixed and delivered to every other rig's RX stream.

The intended outcome: a single machine can host a closed-loop radio-to-radio test bench for codec, modem, and network-layer work — no RF, no extra hardware, no third party.

### Why this is tractable

`wfweb` already contains a complete, 1987-line `icomServer` (`src/radio/icomserver.cpp`) that emulates the Icom LAN protocol — token auth, sequence/retransmit, capabilities exchange, audio codec negotiation, CI-V framing. The audit below confirms that its coupling to a real radio is via clean Qt signal/slot seams, with one small gap that needs a surgical fix.

---

## Architecture

```
          ┌─────────────────┐         ┌─────────────────┐
  wfweb A │  icomUdpHandler │  UDP    │  icomServer #A  │  ← unmodified from wfweb
  client  │  (3 channels)   │◄───────►│  (SERVERCONFIG, │
          └─────────────────┘         │  rigs.size==1)  │
                                      └────────┬────────┘
                                               │ signals
                                               ▼
                                      ┌─────────────────┐
                                      │   virtualRig A  │
                                      │   - civEmulator │  CI-V parse / reply
                                      │   - state: f/m/ │
                                      │     PTT/gains   │
                                      └────────┬────────┘
                                               │ audio in (TX) / audio out (RX)
                                               ▼
                                      ┌──────────────────────┐
                                      │   channelMixer       │ ← broadcast bus
                                      │   (N rigs → N rigs)  │   with attenuation
                                      └──────────────────────┘
                                               ▲
                                      ... same for virtualRig B, C, ...
```

### Per-rig wiring

Each virtual rig is:
1. **One `icomServer` instance** with a `SERVERCONFIG` containing exactly one `RIGCONFIG`. Each uses a unique UDP base port triplet (50001/50002/50003, 50011/50012/50013, …).
2. **One `virtualRig` object** owning:
   - A synthesized `rigCapabilities*` (see Q1 findings, minimum fields documented below).
   - A `civEmulator` that parses incoming CI-V via the `rigServer::haveDataFromServer(QByteArray)` signal and calls `icomServer::dataForServer(QByteArray)` to reply.
   - Connections to `channelMixer` for TX audio (in) and RX audio (out).
3. **One entry in the mixer's bus**, tagged with the rig's index.

### Central mixer

`channelMixer` exposes two Qt slots / signals:
- `void pushTxAudio(int rigIdx, const audioPacket&)` — called when a rig's client sends TX audio and PTT is on.
- `void rxAudioForRig(int rigIdx, const audioPacket&)` — emitted to each rig's `icomServer::receiveAudioData()`.

**MVP behavior**: For every TX packet from rig `i`, the mixer emits to all rigs `j ≠ i` (simple broadcast), optionally scaled by a fixed attenuation (default −20 dB). Sample rate is whatever `icomServer` negotiated — in practice 16 kHz, 16-bit PCM mono is safe.

---

## Key integration findings (from audit)

| # | Finding | Impact |
|---|---------|--------|
| Q1 | `icomServer` reads only `guid`, `civAddr`, `modelName`, `rigName`, `baudRate` from `RIGCONFIG` during capability exchange (`icomserver.cpp:1251–1308`). `rxaudio`/`txaudio` null is handled gracefully — server advertises all sample rates (`icomserver.cpp:1279, 1300`). | Synthetic `rigCapabilities` is cheap to build. |
| Q2 | `icomServer::init()` only binds sockets and wires signals — it does **not** require `rigCaps` pre-populated. Caps can arrive later via `rigServer::receiveRigCaps()` (`src/rigserver.cpp:23`). | Virtual rig can start server first, synthesize caps, then emit the signal. |
| Q3 | The `wfweb` client does **not** auto-query model ID or any state on connect. CI-V traffic is UI-driven. Only pings (type `0x07`) are mandatory and are handled entirely by `icomServer` (not forwarded to the rig backend). | `civEmulator` only needs to answer commands the user's UI actually triggers (freq/mode read/set, PTT, gains). |
| Q4 | `audioPacket` (`include/audioconverter.h:44–53`) = `{seq, time, sent, QByteArray data, guid[16], amplitudePeak, amplitudeRMS, volume}`. `data` is raw codec bytes per the negotiated codec. `icomServer::receiveAudioData()` chunks to 1364 bytes and dispatches. | Mixer operates on raw PCM; force clients to negotiate uncompressed LPCM 16-bit 16 kHz in the synthetic caps. |
| Q5 | **Critical gap**: `icomserver.cpp:635–637` dereferences `radio->rig->dataFromServer(...)` without a null check. Crashes with `radio->rig == nullptr`. **BUT** the pre-existing QT 5.9 `#else` branch at line 640 already does `emit haveDataFromServer(r.mid(0x15))` — the exact signal we need. | 3-line null-guard reuses the existing signal path. See "wfweb patch" below. |

---

## Required change to wfweb (`icomserver.cpp:635–641`)

Minimal, backwards-compatible. In the current code:

```cpp
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
    QMetaObject::invokeMethod(radio->rig, [=]() {
        radio->rig->dataFromServer(r.mid(0x15));;
    }, Qt::DirectConnection);
#else
    #warning "QT 5.9 is not fully supported, multiple rigs will NOT work!"
    emit haveDataFromServer(r.mid(0x15));
#endif
```

Change the `#if` branch to null-guard `radio->rig` and fall through to the existing `emit haveDataFromServer(...)` when null. Connecting to the `haveDataFromServer` signal becomes the virtual rig's CI-V ingestion point.

This is the only change to the existing `wfweb` codebase.

---

## New files

```
tools/virtualrig/
├── virtualrig.pro                     # qmake project, links icomserver/packettypes sources
├── config.example.json                # sample rig roster
└── src/
    ├── main.cpp                       # CLI parse, spawn rigs, start Qt event loop
    ├── virtualrig.{h,cpp}             # per-rig state, owns icomServer + civEmulator
    ├── civemulator.{h,cpp}            # CI-V parse/reply state machine
    └── channelmixer.{h,cpp}           # central audio bus

scripts/
└── testrig.sh                         # bring the full test rig up/down
```

### `virtualrig.pro` sketch

- `TEMPLATE = app`, `QT += core network`, `CONFIG += c++17 console`
- `INCLUDEPATH += ../../include ../../src`
- Compile in (from parent repo): `src/radio/icomserver.cpp`, `src/rigserver.cpp`, `src/radio/icomudpbase.cpp`, `src/radio/icomudpaudio.cpp`, `src/radio/icomudpcivdata.cpp`, `src/cachingqueue.cpp` (dependency of `rigServer`), plus whatever `packettypes.h`-adjacent code pulls in.
- A clean-room alternative: a second `.pro` inside `wfweb.pro` using `SUBDIRS`. Defer until the standalone build is known to work.

### `virtualrig` class responsibilities

- Construct `SERVERCONFIG { lan=true, controlPort, civPort, audioPort, rigs=[one RIGCONFIG] }` with unique ports per instance.
- Construct synthetic `rigCapabilities` — reuse `rigidentities.h` to pick an existing model (e.g., `IC-7610`, civ `0x98`). Populate only the fields actually consumed by `icomServer::sendCapabilities()`.
- Move `icomServer` to its own `QThread` (same pattern as `src/servermain.cpp:472–494`).
- Emit the synthetic `rigCaps` via a signal wired to `icomServer::receiveRigCaps()` right after `init()`.
- Connect `icomServer::haveDataFromServer` → `civEmulator::onCivFromClient` (ingest CI-V).
- Connect `civEmulator::reply` → `icomServer::dataForServer` (push CI-V back).
- Connect `icomServer::haveAudioData` → `channelMixer::pushTxAudio` (gated on PTT state).
- Connect `channelMixer::rxAudioForRig` (filtered by index) → `icomServer::receiveAudioData`.

### `civEmulator` MVP scope

State: `freq`, `mode` (`LSB/USB/CW/RTTY/FM/DV`), `filter`, `data` flag, `ptt`, gains (RF/AF/Mic/Monitor/Power/Comp), S-meter (hardcoded constant or mixer-derived), mode-specific extras (breakout later).

Respond to these CI-V commands:
- `0x03` read freq, `0x05` write freq (5-byte BCD).
- `0x04` read mode, `0x06` write mode (2 bytes: mode+filter).
- `0x14 0x01..0x09` gain reads, `0x14 0x01..0x09 xx xx` gain writes.
- `0x15 0x02` S-meter read (return synth value).
- `0x1C 0x00` PTT read / `0x1C 0x00 0x01` PTT set — this is the trigger to flip the "is transmitting" flag that gates mixer input.
- `0x19 0x00` model ID read — return the chosen civ addr.
- `0x1A 0x05` memory/extension commands — ACK only.
- Any other command: ACK (`0xFB`).
- After any successful set command that changes observable state: emit an unsolicited "transceive" frame so the client UI tracks the virtual state (this matches real rig behavior).

Frame format (CI-V): `0xFE 0xFE <dst> <src> <cmd>... 0xFD`. Parse/build in a single small helper.

### `channelMixer` MVP scope

- Thread-safe `QVector<std::optional<audioPacket>>` or plain mutex-guarded per-rig buffer.
- On `pushTxAudio(i, pkt)`: for each `j ≠ i`, copy `pkt`, apply float attenuation to the `data` buffer (PCM 16-bit LE), emit `rxAudioForRig(j, pkt')`.
- Phase 1: broadcast. Phase 2: filter by frequency tolerance + mode compatibility.
- No resampling needed at MVP — all rigs share the negotiated rate.

### `main.cpp` MVP scope

- Parse CLI / `config.json`: list of `{name, civAddr, basePort, initialFreq, initialMode}` entries.
- Spawn one `virtualRig` per entry.
- Hand them a shared `channelMixer*`.
- Run Qt event loop. SIGINT → graceful shutdown.

### `scripts/testrig.sh` — orchestrator

A small Bash script (POSIX-ish, `set -euo pipefail`) that stands up the whole rig in one shot and tears it down cleanly. Subcommands:

```
./scripts/testrig.sh up [N]       # default N=2
./scripts/testrig.sh down
./scripts/testrig.sh status
./scripts/testrig.sh logs [i]     # tail rig i's log
```

**`up` behavior**:
1. Abort if `build/virtualrig` or `wfweb` binary is missing — prompt the user to run `make`.
2. Create a per-run scratch dir `./.testrig/` holding:
   - `virtualrig.pid`, `virtualrig.log`
   - For each `i` in `0..N-1`: `wfweb_i.pid`, `wfweb_i.log`, `wfweb_i.conf` (settings file)
3. Launch `virtualrig --rigs N` detached, redirect stdout/stderr to `.testrig/virtualrig.log`, write PID.
4. For each `i`:
   - Generate `wfweb_i.conf` from a template that pins:
     - Connection = LAN, host `127.0.0.1`, base port `50001 + i*10`
     - `WebServerPort = 8080 + i` (unique browser UI port per instance)
     - Unique `RigName` / log prefix so sessions don't collide
   - Launch `wfweb -b -s .testrig/wfweb_i.conf -l .testrig/wfweb_i.log`, write PID.
5. Poll each wfweb's web port with `curl -sk https://127.0.0.1:<port>/` (up to ~10s) until it returns, so URLs are only printed once the UI is actually serving.
6. Print a summary block:

```
Test rig is up (N=2).

  Rig #0  "virtual-IC7610-A"   https://127.0.0.1:8080
  Rig #1  "virtual-IC7610-B"   https://127.0.0.1:8081

virtualrig log:  .testrig/virtualrig.log
wfweb logs:      .testrig/wfweb_{0,1}.log

Stop with:       ./scripts/testrig.sh down
```

**`down` behavior**:
- Read every PID file in `.testrig/`, send `SIGTERM`, wait ~3s, escalate to `SIGKILL` if still alive.
- Remove PID files; leave logs in place for post-mortem.
- Idempotent: safe to run when nothing is up.

**`status`**: list each PID file, show whether the process is alive, print the associated URL if so.

**`logs [i]`**: `tail -F` the selected log (or `virtualrig.log` if `i` omitted).

Keep it deliberately simple — one ~100-line Bash file, no systemd units, no Docker. A user running FreeDV/RADE experiments just wants `up`, two browser tabs, and `down` when they're done.

---

## Phasing

| Phase | Scope | Estimate |
|-------|-------|----------|
| **0 — Prep** | Audit & null-guard patch to `icomserver.cpp`. Verify existing `wfweb` build still passes. | 0.5 day |
| **1 — MVP** | 2 virtual rigs, broadcast mixer, minimal CI-V emulator. Prove: wfweb A keys PTT with a tone → wfweb B hears it. Fixed freq, fixed mode (USB), IC-7610 identity hardcoded. | 3–4 days |
| **2 — Usable test bench** | N rigs via `config.json`. Freq/mode-aware routing (only same band+mode hears). Per-link attenuation/noise. S-meter reflects bus level. Run FreeDV RADE between two instances end-to-end. | 3–5 days |
| **3 — Optional polish** | Small HTTP dashboard showing rig state + bus activity. Basic fading/multipath model. USB path via `socat`-pty + `pttyhandler` (no LAN). | open-ended |

Suggested stopping point after Phase 2 — by then FreeDV/RADE development can proceed against the simulator.

---

## Risks & open items

1. **Audio codec negotiation**. `icomServer` honors whatever the client requests. We'll need to either (a) force the synthetic caps to advertise only LPCM-16/16 kHz/mono so the client picks it, or (b) have the mixer decode whatever codec was negotiated. (a) is simpler; (b) is more realistic. MVP picks (a).
2. **`cachingQueue` dependency**. `rigServer` base calls `cachingQueue::getInstance()` in its constructor (`src/rigserver.cpp:11`). Confirm this singleton is safe to instantiate in the virtual-rig process without a real `rigCommander` attached — spot check during Phase 0.
3. **Model choice matters for UI polish**. Some features (spectrum scope, waterfall over LAN) only activate for specific models. Picking `IC-7610` gives a reasonable baseline; scope/waterfall can be faked later or simply left off.
4. **GUID uniqueness**. Each virtual rig needs a stable unique GUID so `wfweb` clients remember it across restarts — derive deterministically from `rigName` or the config file.
5. **Sequence numbers on client restart**. `icomServer` already handles client reconnects; should be transparent.

---

## Critical files

### To modify

- `src/radio/icomserver.cpp:635–641` — null-guard `radio->rig` dereference, fall through to existing `emit haveDataFromServer(...)`.

### To reuse (read-only)

- `src/radio/icomserver.cpp` — the emulated server (compiled into `virtualrig` too).
- `src/rigserver.cpp`, `include/rigserver.h` — base class, `SERVERCONFIG`/`RIGCONFIG` structs.
- `include/packettypes.h` — wire-format packet unions.
- `include/audioconverter.h:44–53` — `audioPacket` definition.
- `include/rigidentities.h` — `rigCapabilities` + existing model templates.
- `src/radio/icomudpbase.cpp`, `icomudpaudio.cpp`, `icomudpcivdata.cpp` — sub-protocol pieces linked transitively.
- `src/cachingqueue.cpp`, `include/cachingqueue.h` — depended on by `rigServer` constructor.
- `src/servermain.cpp:472–494` — reference for threaded-server wiring.

### To create

- `tools/virtualrig/virtualrig.pro`
- `tools/virtualrig/config.example.json`
- `tools/virtualrig/src/main.cpp`
- `tools/virtualrig/src/virtualrig.{h,cpp}`
- `tools/virtualrig/src/civemulator.{h,cpp}`
- `tools/virtualrig/src/channelmixer.{h,cpp}`
- `scripts/testrig.sh`
- `scripts/wfweb.conf.template` — settings-file template the script fills in per instance

---

## Verification

### Phase 0 — patch only
1. `qmake wfweb.pro && make -j$(nproc)` still builds cleanly.
2. Run `wfweb` against a real radio (or the existing server mode) — confirm CI-V still flows, no regression.

### Phase 1 — MVP loop
1. Build everything: `qmake wfweb.pro && make -j$(nproc)` (parent) and `cd tools/virtualrig && qmake && make -j$(nproc)` (simulator).
2. `./scripts/testrig.sh up 2` — script launches `virtualrig`, two `wfweb` instances, and prints the two UI URLs (e.g. `https://127.0.0.1:8080` and `:8081`).
3. Open both URLs in separate browser tabs. Confirm capabilities exchange succeeds and each UI shows an `IC-7610`-shaped rig.
4. In tab A, set frequency via UI; confirm `.testrig/virtualrig.log` shows the CI-V write and A's readback reflects it.
5. In tab A, generate a 1-kHz test tone on the mic input and hold PTT. In tab B's RX audio, verify the tone is audible (attenuated per mixer setting). Confirm B's S-meter rises.
6. Reverse direction: B transmits, A receives.
7. `./scripts/testrig.sh down` — verify all processes exit cleanly and the scratch dir is reusable on next `up`.

### Phase 2 — FreeDV loop
1. Configure both wfweb instances for FreeDV mode (e.g., RADE V1) on the same virtual frequency.
2. Speak into A's browser mic with PTT on; confirm B decodes and shows the decoded callsign / SNR / sync indicators.
3. Test the RADE EOO callsign path end-to-end (already working with real radios — verifies the virtual rig preserves timing well enough).

### Regression
Run through any existing `tests/` scripts that exercise the LAN protocol against a live rig server — the virtual rig should satisfy them.
