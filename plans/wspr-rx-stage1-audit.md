# Stage 1 Audit: WSPR RX Skeleton

## Scope

Audit scope for Stage 1:

- browser WSPR RX state
- cooldown-slot capture
- worker stub lifecycle
- WSPR TX/RX interaction
- WSPR panel state reporting

Files:

- `resources/web/index.html`
- `resources/web/wspr-rx-worker.js`
- `resources/web.qrc`

## Independent Audit Passes

### Audit 1: Timing and Slot Alignment

Finding:

- Slot tracking initially re-anchored each audio chunk on `Date.now()`, which made slot boundaries sensitive to websocket arrival jitter.

Fix applied:

- Added `digiWsprRxSampleClockMs` so slot tracking advances on a continuous sample clock instead of re-anchoring every chunk.
- Added bounded re-sync only when the local sample clock drifts materially.

### Audit 2: Worker Lifecycle and Async Decode

Finding:

- A late worker result could have updated the WSPR panel after a mode switch or panel close.
- Completed RX windows could also be dropped whenever a previous decode was still marked busy.

Fix applied:

- Added `digiWsprDecodeEpoch`.
- Resetting WSPR RX state now increments the epoch and clears in-flight decode state.
- Worker requests echo the epoch, and stale results are ignored.
- Resetting WSPR RX state now terminates the worker and clears any pending decode.
- Added a single-slot pending decode queue instead of dropping a completed cooldown window immediately.

### Audit 3: UX and Reset Semantics

Finding:

- Clearing the queue, disabling TX, or halting could leave the current RX slot marked as `SKIP TX` even after TX was no longer pending.

Fix applied:

- Added `refreshWsprCurrentSlotEligibility()`.
- Recompute current-slot capture eligibility after queue clear, TX disable, halt, and WSPR TX completion.

### Audit 4: Integration Safety

Finding:

- Stage 1 needed a stable static worker asset path and explicit resource packaging to avoid runtime worker load failures.
- A retune during an active cooldown capture could contaminate one RX window with audio from multiple bands.

Fix applied:

- Added `resources/web/wspr-rx-worker.js`
- Added worker asset to `resources/web.qrc`
- Invalidate the current WSPR RX slot immediately when a retune is requested during capture.
- Verified page script and worker syntax locally

## Verification

Local verification completed:

- `git diff --check`
- `node --check resources/web/wspr-rx-worker.js`
- extracted inline page script passed `node --check`

## Residual Risks

- WSPR RX decode is still a stub; no real decoder is present yet.
- UTC alignment is still browser-clock based. Stage 1 now uses a continuous local sample clock, but real decoder work still needs a firmer timing model than local arrival time alone.
- WSPRnet upload is not implemented in Stage 1.
