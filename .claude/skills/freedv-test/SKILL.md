---
name: freedv-test
description: Run an end-to-end FreeDV RADE EOO-callsign round-trip between two virtual wfweb rigs. Boots the testrig bench, puts both rigs in RADE mode, transmits from one by injecting synthesized audio into the server's RADE encoder (the headless browser has no mic), and verifies the other decodes the EOO callsign from the server log. Use when the user asks to test FreeDV, test RADE, or verify the RADE EOO callsign TX/RX path. (RADE only — classic 700D/700E/1600 are out of scope.)
---

Run an end-to-end **FreeDV RADE EOO-callsign round-trip** between two virtual rigs: one station transmits a RADE signal and the other must decode the transmitting station's callsign from the **EOO (End-Of-Over) frame**. Bench-only (virtual rigs, no hardware, no real RF). **RADE only** — the classic FreeDV modes (700D/700E/1600) are out of scope.

This is the **outlier** among the mode-test skills. Unlike CW/FT8/JS8 (browser-side codecs), **RADE runs server-side (C++)**, and it breaks the pure-browser pattern in three ways you must handle:
1. **No browser mic.** The headless Playwright chromium denies `getUserMedia`, so you can't drive a real voice TX. Instead you **inject synthesized audio** straight into the server's RADE encoder by sending `0x03` TX-audio WebSocket frames (the same frame type the mic worklet uses) via `window.sendTxAudio()`.
2. **The assert is the server log, not the DOM.** The decoded callsign appears in the browser for only **5 seconds** (the server clears it via a timer), so reading `window.radeRxCallsign` is racy. The authoritative, race-free signal is the RX rig's log line `RADE: decoded callsign from EOO: "<CALL>"`.
3. **Sync is unreliable with synthetic audio.** This is the honest limitation: feeding the neural RADE codec synthesized (non-speech) audio produces a modem signal that *arrives fine* — the receiver sees a healthy SNR (measured up to **+15 dB**) — but RADE's modem **sync** still fails most of the time, so the EOO doesn't decode. It works (proven both directions), just not dependably: expect to retry several times, and a clean-room run can miss 5×+ in a row. The robust fix is to inject **real speech** instead of tones/noise (see "Improving reliability"). **Retry on miss** and transmit a long burst (~12–15 s).

## Prerequisites (verify before starting; fix and stop if missing)

1. `./wfweb` and `./tools/virtualrig/virtualrig` are built (`qmake wfweb.pro && make -j$(nproc)`; `cd tools/virtualrig && qmake && make -j$(nproc)`). The server build is required — RADE is server-side and disabled in the Standalone bundle.
2. Repo `.mcp.json` includes `--ignore-https-errors` for the playwright MCP. Verify: `pgrep -af @playwright/mcp` must show `--ignore-https-errors`. If `.mcp.json` was just edited, the user must restart Claude Code. Stop and tell them.
3. Load the playwright tools via ToolSearch: `select:mcp__playwright__browser_navigate,mcp__playwright__browser_snapshot,mcp__playwright__browser_click,mcp__playwright__browser_type,mcp__playwright__browser_take_screenshot,mcp__playwright__browser_wait_for,mcp__playwright__browser_evaluate,mcp__playwright__browser_close,mcp__playwright__browser_tabs,mcp__playwright__browser_select_option`.

## Boot the bench

```
./scripts/testrig.sh up 2
```

Both rigs come up on **14.074.000 USB** (FreeDV is a USB data mode — same freq + mode means audio gates and A's RADE signal reaches B). **Prefer a freshly-booted bench**: RADE sync gets flakier after many TX cycles on a long-lived bench.

- A → https://127.0.0.1:9080  (virtual-IC7300-A) — log: `.testrig/wfweb_0.log`
- B → https://127.0.0.1:9090  (virtual-IC7300-B) — log: `.testrig/wfweb_1.log`

## Per-tab UI bring-up (do for both A and B)

1. `browser_navigate` to the rig URL.
2. **Wipe stale browser state**: `browser_evaluate` `() => { localStorage.clear(); sessionStorage.clear(); }`, then `browser_navigate` to the same URL again.
3. Click the **CLICK TO START** splash; poll until `window.audioEnabled === true` (RX audio enables ~1 s later — B's RADE decoder is fed from this RX-audio path).
4. **Set the station callsign** (this becomes the EOO callsign — it's encoded on the first TX frame, so set it *before* transmitting): `window.App.callsign.set('W1AAA')` then `window.sendReporterConfig()` (pushes it to the server's `reporterCallsign`, which sets the RADE TX callsign). A: `W1AAA`, B: `W1BBB`.
5. **Enter RADE:** `window.toggleFreeDV()` (RADE is `freedvModes[0]`, advertised first by the server; opening forces USB). Confirm `window.freedvEnabled === true` and `window.freedvModeName === 'RADE'`.

Stable handles / state:
- Indicators: `#freedvSyncEl` (SYNC/NO SYNC), `#freedvSnrEl`, `#freedvCallsign` (only present while a decoded callsign is showing — clears after 5 s)
- Functions (global): `toggleFreeDV()`, `cycleFreeDVMode()`, `sendReporterConfig()`, `send(obj)` (raw WS command), `sendTxAudio(arrayBuffer)` (raw 0x03 TX-audio frame)
- State (globals on `window`): `freedvEnabled`, `freedvModeName`, `freedvSync`, `freedvSNR`, `radeRxCallsign` (the 5 s flash)

## TX: inject synthesized audio (the receiver-less mic substitute)

The headless browser can't capture a mic, so transmit by streaming synthesized **48 kHz Int16LE** PCM as `0x03` frames. Frame layout: `[0x03][0x00][seq_u16LE][reserved_u16LE][PCM_Int16LE…]`. Run this in `browser_evaluate` **on the transmitting tab, keeping that tab foreground for the whole burst** (background tabs throttle `setTimeout`, stalling the 20 ms pacing and breaking the stream):

```js
async () => {
  const RATE = 48000;
  function frame(seq, tSec) {
    const N = 960; const buf = new ArrayBuffer(6 + N*2); const dv = new DataView(buf);
    dv.setUint8(0,0x03); dv.setUint8(1,0x00); dv.setUint16(2,seq&0xFFFF,true); dv.setUint16(4,0,true);
    const f0 = 140 + 30*Math.sin(tSec*5.0);          // pitch with vibrato
    for (let i=0;i<N;i++){ const t=tSec+i/RATE;
      let v = 0.6*Math.sin(2*Math.PI*f0*t) + 0.3*Math.sin(2*Math.PI*(f0*2)*t)
            + 0.25*Math.sin(2*Math.PI*(f0*3.5)*t) + 0.15*Math.sin(2*Math.PI*(f0*6)*t);
      let s = 0.95*Math.max(-1,Math.min(1,v));        // NEAR FULL-SCALE — quiet audio won't sync
      dv.setInt16(6+i*2, s<0 ? s*32768 : s*32767, true);
    }
    return buf;
  }
  window.send({cmd:'enableMic', value:true});
  await new Promise(r=>setTimeout(r,300));
  window.send({cmd:'setPTT', value:true});
  for (let k=0;k<600;k++){ window.sendTxAudio(frame(k, k*0.020)); await new Promise(r=>setTimeout(r,20)); } // ~12 s
  window.send({cmd:'setPTT', value:false});           // PTT-off → server generates the EOO frame w/ callsign
  await new Promise(r=>setTimeout(r,300));
  window.send({cmd:'enableMic', value:false});
  return 'TX done';
}
```

Two things make or break this: the audio must be **loud** (~0.95 full-scale — a quiet signal lands at ~−5 dB SNR and never syncs), and the burst must be **long** (~12 s, raise to 15 s on retry) so the receiver acquires sync *before* the trailing EOO. A lone EOO with no preceding signal will not decode.

## Round-trip sequence (with retry)

For each direction (A→B, then optionally B→A):
1. On the **TX tab** (foreground), run the injection block above.
2. **Assert via the RX rig's log** — grep for the decode:
   ```
   grep 'RADE: decoded callsign from EOO' .testrig/wfweb_1.log   # B is the RX side for A→B
   ```
   PASS when it shows `decoded callsign from EOO: "W1AAA"`. (For B→A, grep `.testrig/wfweb_0.log` for `"W1BBB"`.)
3. **If no decode, retry** the injection (sync is probabilistic). Bump the burst to ~15 s (`k<750`). Give up after ~4 attempts and report the SNR you saw (`window.freedvSNR` on the RX tab) — a floor value (~−5) means the signal isn't arriving; a higher value that still won't sync means marginal modem lock.
4. Optionally, to screenshot the 5 s browser flash: right after a successful TX, switch to the RX tab and poll `window.radeRxCallsign` tightly (every ~120 ms); the instant it's truthy, `browser_take_screenshot`. This is best-effort — the log is the real evidence.

Save screenshots as `.playwright-mcp/wfweb-rade-A.png` / `...-B.png` (project-dir/`.playwright-mcp/` only; `/tmp` is rejected).

## Improving reliability (recommended before relying on this skill)

The tone/noise synthetic audio above is the weak link — it gives good SNR but marginal sync. Two more reliable approaches, in order of preference:
- **Inject real speech.** Fetch a short speech WAV (mono, any rate — resample to 48 kHz Int16 in JS), and stream its samples as the `0x03` payload instead of the synthesized tones. Real speech drives the RADE encoder to produce a properly-syncable modem signal, which is what RADE was trained on. This is the right long-term fix.
- **Enable a fake mic device in the MCP** (`--use-fake-device-for-media-stream --use-fake-ui-for-media-stream` on the chromium launch) so `getUserMedia` returns Chromium's fake audio and the *real* mic→RADE path works. This needs a `.mcp.json` change + Claude Code restart, so confirm with the user first.

Until one of those is in place, treat a PASS as "decoded on retry" and a no-decode-after-N as inconclusive (capability is proven; the synthetic-audio test is just flaky), not a regression.

## Teardown and report

1. `browser_close` (both tabs), then `./scripts/testrig.sh down`.
2. Report PASS/FAIL per direction with the decode log line(s) as evidence. PASS = the RX rig's log shows `RADE: decoded callsign from EOO: "<the TX station's call>"`.

## Pitfalls — read before debugging

- **`getUserMedia` is denied headless** (`NotAllowedError`) — there is no browser mic. You MUST inject `0x03` audio via `sendTxAudio()`; the normal mic/PTT UI path produces no TX audio. (Server confirms the route: a `0x03` frame in RADE mode is fed to the RADE encoder.)
- **Audio level matters.** ~0.95 full-scale syncs; quiet audio sits at ~−5 dB SNR on the RX side and never locks. If B shows `freedvSNR === -5` throughout, your TX is too quiet (or not arriving).
- **Burst length matters.** RADE RX needs sustained signal to acquire sync *before* the EOO. ~12 s works ~half the time; 15 s is safer. A lone EOO (e.g. PTT-cycling with no injected audio) transmits the callsign frame but B never syncs to decode it — confirmed.
- **Sync is probabilistic — retry.** Even done right, expect ~40–60% success per attempt with synthetic audio. Loop up to ~4 times. A fresh bench is more reliable than one that's run many TX cycles.
- **Assert on the server log, not the DOM.** `window.radeRxCallsign` (and `#freedvCallsign`) is cleared after **5 s** by `radeCallsignClearTimer`. The log line is permanent and race-free. Don't gate on `freedvSync` either — the callsign arrives in the EOO *after* sync is already lost (this is by design; see CLAUDE.md).
- **Keep the TX tab foreground** during the ~12–15 s injection. Switching away throttles `setTimeout` and stalls the 20 ms frame pacing.
- **Callsign must be set before TX** — it's baked into the EOO on the first TX frame (`setTxCallsign` ← `reporterCallsign` ← `App.callsign`).
- **Both rigs USB + same freq.** `toggleFreeDV()` sets USB; the bench defaults both to 14.074. If they diverge, B won't gate A's audio in.
- **Don't run two RADE TXs at once.** One TX at a time across the bench; let one direction's EOO decode before starting the other.
- **Never transmit on a physically-connected rig** — virtual-bench only.
