# rig-bench.md — bringing up wfweb against the real IC-7300

Shared bring-up procedure for all per-mode agents (mode-cw / mode-js8 / mode-ft8 /
mode-packet / mode-freedv). Both builds talk to the **same physical IC-7300** on
`/dev/ttyUSB0` (CP210x `0x10C4:0xEA60`).

## ⛔ SAFETY — NEVER TRANSMIT ON THE REAL RADIO
The IC-7300 is connected to a real antenna. **Any transmission is forbidden** — it
emits actual RF, which is dangerous and illegal outside proper operating conditions.
When a test touches the real rig you may do **RX / read-only control ONLY**:
- ALLOWED: read frequency / mode / meters, switch RX filters, decode received audio,
  run in-browser codec roundtrips (pure DSP — the synthesized audio never reaches the rig).
- **NEVER**: PTT / `setPTT` on, CW keying (`/api/v1/radio/cw`, rigctld `b` / `T 1`), any
  FT8 / JS8 / packet / FreeDV *send* or *transmit*, antenna tune, `startDigiTune`,
  `streamDigiAudio`→rig, or enabling the mic into the rig.
Validate every TX path WITHOUT keying the real rig: use the **virtual rig** (`?virtual=1`,
BroadcastChannel — no RF), loopback self-tests (`--packet-self-test`), or code inspection.
If a check seems to require transmitting on the real radio, **STOP and report it as
needs-virtual / needs-bench** — never key the physical rig.

## GOLDEN RULE: one rig, one browser — serialize everything
The serial port is exclusive. The Server build holds `/dev/ttyUSB0` directly; the
Standalone browser holds it via Web Serial. They **cannot both own it**. And there is
exactly **one** Playwright browser. Therefore:
- Never run two mode agents' hardware tests concurrently — the coordinator queues them.
- Within one agent: fully tear down the Server phase BEFORE the Standalone phase (and vice-versa).
- Free the port before each phase: `lsof /dev/ttyUSB0` must be empty, `pgrep -af 'wfweb -b'`
  (kill a stray), and close any open browser tab (releases Web Serial).

## Phase 1 — Server build
1. Build if needed: `qmake wfweb.pro && make -j$(nproc)` (or the `/build` skill).
2. Port free? `lsof /dev/ttyUSB0` empty; else stop the holder.
3. Boot (auto-connects to the '7300 — no settings file needed):
   `cd $CLAUDE_PROJECT_DIR && ./wfweb -b -l /tmp/wfweb-bench.log` — wait ~2 s for :8081/:8080.
4. Drive the UI at `https://localhost:8080` (self-signed cert; MCP runs `--ignore-https-errors`).
5. REST is on :8081 (e.g. `curl -s http://localhost:8081/api/v1/radio`).
6. Teardown: `pkill -f 'wfweb -b'` (only if you started it; leave a pre-existing dev instance).

## Phase 2 — Standalone build
1. Build the bundle: `tools/build-static.sh dist/`
2. Serve: `tools/serve-static.py` (NO args — fixed dir `<repo>/dist`, fixed port 8000).
   "port 8000 in use" → a server is already up; reuse it.
3. Ensure the Server build is stopped and `/dev/ttyUSB0` is free.
4. Open `http://localhost:8000`. The splash button must read **"CLICK TO START"** — that
   means `getPorts()` saw the rig via the managed policy. If it reads **"PAIR USB RIG"**,
   the grant is broken (see policy note); do NOT click — that path opens the `requestPort()`
   chooser, which can't be auto-dismissed.
5. Click "CLICK TO START" → `tryAutoReconnect()` silently opens the rig. Status bar shows
   "Rig connected" + a live frequency. (The click also satisfies the audio user-gesture.)

## Web Serial policy (already installed — context only)
- `/etc/opt/chrome/policies/managed/wfweb-serial.json` grants the '7300's USB IDs
  (`vendor 4292 / product 60000`) to `http://localhost:8000` + `127.0.0.1:8000` via
  `SerialAllowUsbDevicesForUrls`. Honored only by **system Chrome** (`.mcp.json` uses
  `--browser chrome`); Playwright's bundled Chromium ignores policies.
- If you change the policy file, Chrome must relaunch to re-read it: find the MCP Chrome
  with `ps -ef | grep '[m]cp-chrome'` (bracket trick avoids self-match), kill that profile,
  then `browser_navigate` relaunches fresh. Do NOT `pkill -f mcp-chrome-<id>` with the literal
  string in argv — it matches its own shell.

## AUDIO — NOT yet hands-off (pending)
Rig RX/TX audio (`getUserMedia` on the '7300 USB codec) currently fails with
`NotAllowedError`. **Real off-air audio RX from the rig is not testable yet.**
PREFER audio-free tests: feed known samples straight into a codec/decoder, use
`--packet-self-test`, or the virtual-rig `packet-test` skill. Use the real rig for
CONTROL (freq / mode / PTT / keying), which works headless today. Flag anything that
needs real audio as **blocked-on-audio** — never fake a pass.

## Browser gotchas
- Screenshots must be saved under `.playwright-mcp/` (MCP rejects paths elsewhere).
- Indicator spans use stable IDs (`freedvSyncEl`, `freedvSnrEl`, `freedvCallsign`,
  `freedvFoEl`) — never `nth-child`.
- Report concrete values seen, never "looks ok". Don't fake success.
