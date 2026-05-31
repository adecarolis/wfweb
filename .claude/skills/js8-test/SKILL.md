---
name: js8-test
description: Run an end-to-end JS8 message round-trip between two virtual wfweb rigs. Boots the testrig bench, drives both browsers via Playwright MCP, has each station send a directed JS8 message via the compose box while the other decodes it into its feed, verifies the copy, screenshots both, and tears down. Use when the user asks to test JS8, run a JS8 QSO, verify the JS8 codec/panel, or validate the JS8 messenger TX/RX path. (wfweb-vs-wfweb on the bench — not interop against real JS8Call.)
---

Run an end-to-end **JS8 message round-trip** between two virtual rigs: each station sends a directed message from the compose box, and the other must decode it into its feed. Drive both browsers via Playwright MCP, screenshot both sides, tear down. Bench-only (virtual rigs, no hardware, no real RF) — this tests wfweb's JS8 codec against itself, not interop with real JS8Call.

Sibling of `packet-test` / `ft8-test` / `cw-test`; reuses the same bench (`scripts/testrig.sh`) and Playwright lifecycle. JS8 is browser-side WASM like FT8, so it shares FT8's **RX-audio gate** and **slot timing**. Read "Pitfalls" before debugging.

## Prerequisites (verify before starting; fix and stop if missing)

1. `./wfweb` and `./tools/virtualrig/virtualrig` are built. If not: `qmake wfweb.pro && make -j$(nproc)` and `cd tools/virtualrig && qmake && make -j$(nproc)`.
2. Repo `.mcp.json` includes `--ignore-https-errors` for the playwright MCP. Verify: `pgrep -af @playwright/mcp` must show `--ignore-https-errors`. If `.mcp.json` was just edited, the user must restart Claude Code (a `/mcp` reconnect re-uses cached args). Stop and tell them.
3. Load the playwright tools via ToolSearch: `select:mcp__playwright__browser_navigate,mcp__playwright__browser_snapshot,mcp__playwright__browser_click,mcp__playwright__browser_type,mcp__playwright__browser_take_screenshot,mcp__playwright__browser_wait_for,mcp__playwright__browser_evaluate,mcp__playwright__browser_close,mcp__playwright__browser_tabs,mcp__playwright__browser_select_option`.

## Boot the bench

```
./scripts/testrig.sh up 2
```

Both rigs come up on **14.074.000**. Opening the JS8 panel auto-retunes each rig to the **JS8 dial frequency** for the band (14.078.000 on 20 m) and forces USB — both move together, so they stay matched and audio gates.

- A → https://127.0.0.1:9080  (virtual-IC7300-A)
- B → https://127.0.0.1:9090  (virtual-IC7300-B)

## Per-tab UI bring-up (do for both A and B)

The JS8 panel is an overlay (`#js8Bar`), opened from the MODE overlay's **JS8** button — it's a peer of the FT8 DIGI bar (opening one closes the other).

1. `browser_navigate` to the rig URL.
2. **Wipe stale browser state**: `browser_evaluate` `() => { localStorage.clear(); sessionStorage.clear(); }`, then `browser_navigate` to the same URL again. (Note: JS8 persists `js8Open=1`, so without the wipe a reload reopens the panel mid-init.)
3. Click the **CLICK TO START** splash (gesture needed for `AudioContext`).
4. **Wait for RX audio.** JS8's decoder is fed by `window._js8ProcessAudioChunk`, which the host audio path only calls when audio is flowing. The splash enables it ~1 s *asynchronously* — poll until `window.audioEnabled === true` before continuing.
5. **Open the JS8 panel:** `window.toggleJs8Bar()` (equivalently MODE → JS8). This forces USB, retunes to the JS8 dial freq, creates the decoder, and enables RX. Confirm `window.js8MessengerVisible() === true` and `window.js8Diag().rxEnabled === true`.
6. **Set MY CALL:** open settings (`#js8SettingsBtn`), set `#js8SettingMycall`, then **close** (`#js8SettingsCloseBtn`). The callsign is committed to the shared station call (`window.App.callsign`) **on close**, not on input — so you must click close. Verify `window.App.callsign.get()` returns it.
   - A: `W1AAA`.  B: `W1BBB`. (Shared station call — same field feeds CW/FT8/FreeDV/APRS.)
   - Submode is also read on close; the default **JS8 Normal · 15 s** (`submode 0`) is fine. RX decodes *all* submodes regardless, so the TX submode only affects the sender's frame timing.

When refs go stale across snapshots, drive directly via `browser_evaluate` with stable IDs (the normal path, not a fallback):
- Buttons: `js8SettingsBtn`, `js8SettingsCloseBtn`, `js8SendBtn`, `js8RxToggle`, `js8TuneBtn`, `js8CloseBtn`
- Fields: `js8DxCall` (the TO target — authoritative), `js8Compose` (message text), `js8SettingMycall`, `js8SubmodeSel`
- **Feed (decoded + sent messages): `#js8Feed` `.textContent`** ← assert on this. Rows read `<ts><from>→<target>│<msg><snr>dB`; your own sent rows show `SENT n/n`.
- Functions (global): `toggleJs8Bar()`, `js8MessengerVisible()`, `js8Diag()` (returns `{rxEnabled, txBusy, submode, decodes, audioChunks, …}`), `js8ClearFeed()`
- Note: the internal state object `S` is **not** on `window` (unlike `window.Packet.state`). Use `js8Diag()` for status and read `#js8Feed` for messages.

## QSO sequence (single bidirectional run)

1. **A → send directed message**: on tab A, set `#js8DxCall` to `W1BBB` (dispatch `input`), set `#js8Compose` to `HELLO` (dispatch `input`), click `#js8SendBtn`. This routes through `enqueueTx`, which **slot-aligns** the transmission. Use the compose+send path — do NOT use `window.txJs8()` (a raw 12-char test hook that streams un-slot-aligned audio and won't reliably decode).
2. **Confirm A transmits**: poll `window.js8Diag().txBusy` — it goes true on the next slot boundary, then false when done. A's own feed row shows `W1AAA→W1BBB │ HELLO  SENT 2/2`. A short message is ~2 frames; with the wait-for-slot + TX time this takes ~25–35 s at Normal.
3. **Read B's copy**: on tab B, poll `#js8Feed`.textContent until it contains both `W1AAA` and `HELLO` (allow ~30 s after A finishes). On the clean virtual channel the decode is solid (SNR ~+20 dB). **Assert tolerantly** — require the sender call and a recognizable chunk of the message, not byte-exact.
4. **B → reply**: on tab B, set `#js8DxCall` to `W1AAA`, `#js8Compose` to `GE TNX`, click `#js8SendBtn`.
5. **Read A's copy**: on tab A, poll `#js8Feed` until it contains `W1BBB` and `GE`.
6. **Screenshot both** with `browser_take_screenshot`, `fullPage: true`. Save as `.playwright-mcp/wfweb-js8-A.png` and `...-B.png`. (Bare names land in the project root; `/tmp` is rejected — the MCP roots are the project dir and `.playwright-mcp/`.)

## Teardown and report

1. `browser_close` (both tabs), then `./scripts/testrig.sh down`.
2. Report PASS/FAIL and the two screenshot paths. PASS = each side decoded the other's directed message (sender call + message text appear in its `#js8Feed`).

## Pitfalls — read before debugging

- **RX-audio gate (same as FT8).** The decoder only runs while `window.audioEnabled === true` (enabled ~1 s after CLICK TO START). Gate on it. `js8Diag().audioChunks` should be climbing; if it's stuck at 0, audio isn't flowing.
- **Callsign commits on settings CLOSE.** Typing into `#js8SettingMycall` does nothing until you click `#js8SettingsCloseBtn` — that's where `App.callsign.set()` runs. Verify via `window.App.callsign.get()`.
- **Use compose + `#js8SendBtn`, not `window.txJs8()`.** `txJs8` requires an exact-12-char message and streams immediately without slot alignment — JS8's per-slot decoder usually won't catch it. The compose path slot-aligns via `enqueueTx`, which is what actually works.
- **JS8 is slot-timed (Normal = 15 s).** The send waits for the next slot boundary, then transmits over one or more 15 s frames. Budget ~30 s per direction; poll, don't expect instant. `js8Diag().submode` confirms the TX submode; a faster submode (Fast 10 s / JS8 60 4 s) speeds the run but RX decodes all modes either way.
- **TO field (`#js8DxCall` → `S.dxCall`) is the authoritative target**, not the active QSO tab. Set it before each send. Empty defaults to `@ALLCALL` (a broadcast, which B still decodes — fine if you prefer the simpler broadcast assert).
- **`S` is not on `window`.** Read message state from `#js8Feed`.textContent and status from `window.js8Diag()`. Don't look for `window.js8.state` or similar.
- **The feed mixes sent and received rows.** Your own sent message shows with `SENT n/n`; the received copy on the *other* tab shows an `…dB` SNR. Assert the receiver's feed has the *sender's* call — don't accidentally match the sender's own `SENT` row.
- **Don't skip `localStorage.clear()`** — JS8 persists `js8Open` and the last callsign; a clean slate avoids reopening the panel mid-init and stale calls.
- **Stale Playwright `ref=` handles** re-render constantly; drive via `browser_evaluate` + the stable IDs above.
- **Screenshots are sandboxed** to the project dir / `.playwright-mcp/`; `/tmp` is rejected.
- **Never transmit on a physically-connected rig** — virtual-bench only (no real RF).
