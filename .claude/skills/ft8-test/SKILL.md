---
name: ft8-test
description: Run an end-to-end FT8 decode round-trip between two virtual wfweb rigs. Boots the testrig bench, drives both browsers via Playwright MCP, has each station CALL CQ on opposite slot parity, verifies each side decodes the other's CQ, screenshots both, and tears down. Use when the user asks to test FT8, run an FT8 QSO/decode, verify the ft8ts decoder, or validate the DIGI panel TX/RX path. (FT4 is a fast variant of the same panel — see the FT4 note; not run by default.)
---

Run an end-to-end **FT8 decode round-trip** between two virtual rigs: each station calls CQ on opposite slot parity, and each must decode the other's CQ. Drive both browsers via Playwright MCP, screenshot both sides, tear down. This is bench-only (virtual rigs, no hardware, no real RF).

This skill is the FT8 sibling of `packet-test` and reuses the same bench (`scripts/testrig.sh`) and Playwright lifecycle. Read the "Pitfalls" section before debugging — FT8 has one gotcha packet doesn't (RX audio must be enabled for the decoder to see anything).

## Prerequisites (verify before starting; fix and stop if missing)

1. `./wfweb` and `./tools/virtualrig/virtualrig` are built. If not: `qmake wfweb.pro && make -j$(nproc)` and `cd tools/virtualrig && qmake && make -j$(nproc)`.
2. Repo `.mcp.json` includes `--ignore-https-errors` for the playwright MCP (wfweb's cert is self-signed; without it chromium fails with `ERR_CERT_AUTHORITY_INVALID`). Verify the running MCP: `pgrep -af @playwright/mcp` must show `--ignore-https-errors`. If `.mcp.json` was just edited, the user must restart Claude Code (a `/mcp` reconnect re-uses cached args). Stop and tell them.
3. Load the playwright tools via ToolSearch: `select:mcp__playwright__browser_navigate,mcp__playwright__browser_snapshot,mcp__playwright__browser_click,mcp__playwright__browser_type,mcp__playwright__browser_take_screenshot,mcp__playwright__browser_wait_for,mcp__playwright__browser_evaluate,mcp__playwright__browser_close,mcp__playwright__browser_tabs,mcp__playwright__browser_select_option`.

## Boot the bench

```
./scripts/testrig.sh up 2
```

Both rigs come up on **14.074.000 USB** — which is the 20 m FT8 dial frequency, so no retune is needed and audio gates (matching freq+mode) just like real FT8.

- A → https://127.0.0.1:9080  (virtual-IC7300-A)
- B → https://127.0.0.1:9090  (virtual-IC7300-B)

## Per-tab UI bring-up (do for both A and B)

The FT8 panel is the **DIGI bar**, toggled from the MODE overlay (the `FT8` button toggles the bar; it does NOT change rig mode — opening it forces USB + the FT8 dial freq automatically).

1. `browser_navigate` to the rig URL.
2. **Wipe stale browser state** (Playwright reuses its profile across runs): `browser_evaluate` `() => { localStorage.clear(); sessionStorage.clear(); }`, then `browser_navigate` to the same URL again.
3. Click the **CLICK TO START** splash (browsers refuse `AudioContext` without a gesture).
4. **Wait for RX audio to come up.** This is the FT8-specific step packet doesn't need: the decoder only runs on RX audio, and the splash enables it *asynchronously* ~1 s after START. Poll until `window.audioEnabled === true` before continuing — otherwise no audio reaches the decoder and nothing ever decodes.
5. Open MODE, then click the **FT8** button (find by text `FT8` among visible buttons). Confirm `window.digiBarVisible === true`.
6. Open settings (`#digiSettingsBtn`), set **MY CALL** (`#digiMyCall`) and **GRID** (`#digiGrid`), then close (`#digiSettingsCloseBtn`). Dispatch `input` events so the handlers fire.
   - A: `W1AAA` / `FN20`.  B: `W1BBB` / `FN21`.
   - **Both call AND grid are required** — `digiCallCq()` silently aborts (and pops the settings modal) if either is missing. The callsign is the shared station call (CW/FT8/JS8/FreeDV/APRS) — that's by design.
7. **Set opposite slot parity.** A keeps the default **TX EVEN** (`digiCqParity === 0`). On B, click `#digiCqParityBtn` once to flip it to **TX ODD** (`digiCqParity === 1`). Opposite parity is mandatory: a station can't receive while it transmits, so if both call CQ on the same parity they transmit in the same slot and never hear each other.

When refs go stale across snapshots, drive directly via `browser_evaluate` with stable IDs (this is the normal path, not a fallback):
- Buttons: `digiSettingsBtn`, `digiSettingsCloseBtn`, `digiCqParityBtn`, `digiCallCqBtn`, `digiEnableTxBtn`, `digiHaltBtn`, `digiTuneBtn`, `digiCloseBtn`
- Fields: `digiMyCall`, `digiGrid`, `digiDxCall`
- Functions (global): `digiCallCq()`, `getMyCall()`, `getMyGrid()`, `toggleDigiBar()`, `getDigiSlotInfo()`
- State (globals on `window` — classic script, NOT a namespaced object): `digiDecodes` (array of `{msg, snr, dt, freq, time, slotIndex}`), `digiMode` (`'FT8'`/`'FT4'`), `digiCqParity`, `digiTxEnabled`, `digiTxArmed`, `digiTxQueued`, `digiTxActive`, `audioEnabled`, `digiBarVisible`

## QSO sequence (single run — FT8)

1. **Both → CALL CQ**: on each tab call `window.digiCallCq()` (or click `#digiCallCqBtn`). This arms `CQ <call> <grid>` and enables TX. Confirm `digiTxQueued` is `"CQ W1AAA FN20"` on A and `"CQ W1BBB FN21"` on B.
2. **Wait for decodes** (FT8 slots are 15 s; a decode appears only after a full slot completes). Poll up to ~75 s:
   - On **A**: `window.digiDecodes` contains an entry whose `msg` matches `CQ W1BBB FN21`.
   - On **B**: `window.digiDecodes` contains an entry whose `msg` matches `CQ W1AAA FN20`.
   - In practice the first decode lands in ~10–20 s. Each entry carries a real `snr`/`dt`/`freq` — sanity-check the SNR is finite (a genuine decode, not an empty array).
3. **Screenshot both** with `browser_take_screenshot`, `fullPage: true`. Save as `.playwright-mcp/wfweb-ft8-A.png` and `...-B.png`. (The MCP does NOT auto-target `.playwright-mcp/` — bare names land in the project root; `/tmp` is rejected as outside the allowed roots.)
4. **Halt TX** on both: click `#digiHaltBtn` (sets `digiTxEnabled = false`). Good hygiene even on a virtual rig.

## Teardown and report

1. `browser_close` (both tabs), then `./scripts/testrig.sh down`.
2. Report PASS/FAIL and the two screenshot paths. PASS = each side decoded the other's CQ with a finite SNR.

## Pitfalls — read before debugging

- **The decoder needs RX audio.** `handleAudioData()` returns early when `audioEnabled` is false (no PCM reaches `processDigiAudioChunk`). CLICK TO START enables audio ~1 s later, so **gate on `window.audioEnabled === true`** before expecting decodes. This is the #1 cause of "nothing decodes".
- **Opposite parity is mandatory.** Same parity on both ⇒ simultaneous TX ⇒ each is deaf during the other's transmission ⇒ zero decodes. A = TX EVEN, B = TX ODD.
- **Call + grid both required.** No grid ⇒ `digiCallCq()` aborts and opens the settings modal; you'll see TX never arm.
- **FT8 is slot-timed.** Don't expect an instant decode — poll for up to ~75 s (a few 15 s slots). `getDigiSlotInfo()` returns `{slotIndex, slotPhase, remaining, period}` if you need to reason about timing.
- **`digiDecodes` is a window global**, not `window.Something.state`. Read `window.digiDecodes` directly. (Packet uses `window.Packet.state`; FT8 does not — it predates that pattern and uses top-level `var`s.)
- **Don't skip `localStorage.clear()`** — prior runs leave a saved callsign/grid and could boot into a stale state. Clean-slate mirrors a fresh user.
- **Stale Playwright `ref=` handles** re-render constantly; drive via `browser_evaluate` + the stable IDs above.
- **Screenshots are sandboxed** to the project dir / `.playwright-mcp/`; `/tmp` is rejected.
- **FT4 variant (optional, not run by default):** clicking the mode label (`#digiModeLabel`) toggles FT8 ↔ FT4; FT4 uses 7.5 s slots (`digiPeriod()` returns 7.5) so the round-trip is ~2× faster. To test FT4, toggle it on **both** tabs before CALL CQ and match the assert messages (same `CQ <call> <grid>` text). Both rigs must be in the same submode or they won't decode each other.
