# Browser WSPR RX + WSPRnet Plan

## Goal

Add browser-only `WSPR RX` to `wfweb` so the browser can:

- capture WSPR receive windows during cooldown slots
- decode WSPR off the main thread
- show spots and decoder state in the existing DIGI/WSPR UI
- upload decoded spots to `WSPRnet`

The current `WSPR TX` path stays intact and remains browser-driven.

## Constraints

- Keep the receive pipeline in the browser.
- Reuse the existing DIGI PCM websocket path instead of the speaker/WebRTC path.
- Preserve the current deterministic WSPR TX scheduler and band hopping.
- Do not block the main thread during decode.
- Stage work with an audit gate after every implementation stage.

## Reconciled Architecture

### Browser RX path

- Reuse `handleAudioData()` and `processDigiAudioChunk()` in `resources/web/index.html`.
- Add a dedicated WSPR RX window:
  - `120 s`
  - `12 kHz`
  - UTC-aligned to WSPR slot boundaries
- Track slot eligibility so TX slots are never decoded.
- Move WSPR decode to a dedicated worker separate from the FT8/FT4 worker.

### Decoder strategy

- Phase 1: worker stub and plumbing
- Phase 2: real decoder backend, likely a narrow `wsprd`-to-WASM port
- Keep FT8/FT4 decode unchanged

### Upload strategy

- Planned default: browser decode results post to `wfweb`
- Planned default: `wfweb` proxies multipart upload to `WSPRnet`
- Do not rely on direct browser-to-WSPRnet upload by default
  - likely CORS risk
  - weaker dedupe/retry behavior
  - less reliable long-run unattended operation

## Stages

### Stage 1: WSPR RX Skeleton

Deliverables:

- WSPR RX state in `index.html`
- UTC-aligned cooldown-slot capture buffer
- WSPR worker stub
- WSPR RX status UI
- WSPR spot list scaffolding
- WSPR upload status scaffolding

Out of scope:

- real decoding
- network upload

Acceptance:

- entering WSPR mode shows RX-specific state
- cooldown slots are tracked separately from TX slots
- a completed RX slot triggers the WSPR worker stub
- UI shows the slot summary and zero-spot placeholder cleanly

Audit gate:

- slot timing / UTC alignment
- TX/RX isolation
- UI consistency
- browser memory / responsiveness

### Stage 2: Real Decoder Integration

Deliverables:

- WSPR decoder module
- worker-driven real decode
- known-vector verification path
- robust failure reporting

Acceptance:

- completed 120 s buffers can be decoded off-thread
- results are returned as structured WSPR spots
- decode failures do not wedge TX scheduling

Audit gate:

- decoder correctness
- sample-rate / buffer integrity
- worker isolation
- bad-buffer and drift handling

### Stage 3: Spots UI, Persistence, Telemetry

Deliverables:

- spot table
- decode history
- dedupe
- local persistence for recent spots
- richer RX telemetry

Acceptance:

- decoded spots render cleanly
- refresh preserves recent results
- duplicates are suppressed

Audit gate:

- dedupe logic
- persistence cleanup
- table correctness
- malformed data resilience

### Stage 4: WSPRnet Upload

Deliverables:

- server-side upload endpoint in `wfweb`
- browser upload toggle and queue
- multipart upload to `WSPRnet`
- upload result telemetry

Acceptance:

- decoded spot batches can be handed to `wfweb`
- `wfweb` validates, dedupes, and uploads
- failures surface cleanly in UI and logs

Audit gate:

- payload correctness
- retry / dedupe safety
- config/privacy behavior
- failure and recovery behavior

### Stage 5: End-to-End Reliability

Deliverables:

- clock-health warnings
- stalled tab / background throttling detection
- reconnect/reload recovery
- long-run validation fixes

Acceptance:

- unattended operation degrades visibly and safely
- TX and RX do not corrupt each other over long runs
- upload state survives transient disconnects safely

Audit gate:

- long-run stability
- slot race conditions
- reconnect behavior
- user-facing recovery states

## TODO List

1. Add WSPR RX state variables and helper functions.
2. Add WSPR RX panel rows and a spot table placeholder.
3. Add a dedicated WSPR RX worker script and resource wiring.
4. Capture UTC-aligned cooldown-slot audio into a WSPR buffer.
5. Trigger a worker stub on completed RX slots.
6. Add WSPR RX status and last-decode summaries.
7. Run four independent SWE audits and reconcile optional fixes.
8. Port or wrap a real WSPR decoder for the worker.
9. Verify decoder output against known vectors.
10. Persist recent spots locally and render them in the WSPR panel.
11. Add a server-side WSPRnet proxy path.
12. Add browser upload control, queueing, retry, and telemetry.
13. Add long-run health warnings and reliability guards.

## File Impact Forecast

Primary:

- `resources/web/index.html`
- `resources/web.qrc`
- `resources/web/wspr-rx-worker.js`

Later stages:

- decoder glue under `resources/web/`
- decoder `.wasm` asset under `resources/web/`
- `src/webserver.cpp`
- `include/webserver.h`

## Notes

- The current WSPR TX path is already good enough to preserve.
- WSPR RX should not be bolted into the FT8 buffer lifecycle.
- WSPRnet upload should be proxied by `wfweb`, not done directly from the browser by default.
