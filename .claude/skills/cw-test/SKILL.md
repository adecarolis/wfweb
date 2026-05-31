---
name: cw-test
description: Run an end-to-end CW (morse) decode round-trip between two virtual wfweb rigs. Boots the testrig bench, drives both browsers via Playwright MCP, has each station key a known string with the keyer while the other decodes it with the built-in CW decoder, verifies the copy, screenshots both, and tears down. Use when the user asks to test CW, run a CW QSO, verify the CW decoder, or validate the keyer TX / decode RX path.
---

Run an end-to-end **CW decode round-trip** between two virtual rigs: each station keys a known string (server-side keyer → virtualrig morse synthesis) and the other must decode it with the built-in CW decoder. Drive both browsers via Playwright MCP, screenshot both sides, tear down. Bench-only (virtual rigs, no hardware, no real RF).

This skill is a sibling of `packet-test` / `ft8-test` and reuses the same bench (`scripts/testrig.sh`) and Playwright lifecycle. Read "Pitfalls" before debugging — CW shares FT8's RX-audio gate **and** needs the DECODE toggle on.

## Prerequisites (verify before starting; fix and stop if missing)

1. `./wfweb` and `./tools/virtualrig/virtualrig` are built. If not: `qmake wfweb.pro && make -j$(nproc)` and `cd tools/virtualrig && qmake && make -j$(nproc)`. (virtualrig must include CW synthesis — `synthesizeCw` in `tools/virtualrig/src/virtualrig.cpp`; any recent build has it.)
2. Repo `.mcp.json` includes `--ignore-https-errors` for the playwright MCP. Verify: `pgrep -af @playwright/mcp` must show `--ignore-https-errors`. If `.mcp.json` was just edited, the user must restart Claude Code (a `/mcp` reconnect re-uses cached args). Stop and tell them.
3. Load the playwright tools via ToolSearch: `select:mcp__playwright__browser_navigate,mcp__playwright__browser_snapshot,mcp__playwright__browser_click,mcp__playwright__browser_type,mcp__playwright__browser_take_screenshot,mcp__playwright__browser_wait_for,mcp__playwright__browser_evaluate,mcp__playwright__browser_close,mcp__playwright__browser_tabs,mcp__playwright__browser_select_option`.

## Boot the bench

```
./scripts/testrig.sh up 2
```

Both rigs come up on **14.074.000**. The skill puts both into **CW** mode on the same frequency, so audio gates (matching freq+mode) and A's keyed morse reaches B as RX audio.

- A → https://127.0.0.1:9080  (virtual-IC7300-A)
- B → https://127.0.0.1:9090  (virtual-IC7300-B)

## Per-tab UI bring-up (do for both A and B)

CW is a **rig mode** (unlike FT8's bar toggle): you switch the rig into CW, then open the CW bar.

1. `browser_navigate` to the rig URL.
2. **Wipe stale browser state**: `browser_evaluate` `() => { localStorage.clear(); sessionStorage.clear(); }`, then `browser_navigate` to the same URL again.
3. Click the **CLICK TO START** splash (gesture needed for `AudioContext`).
4. **Wait for RX audio.** The CW decoder taps the RX-audio playback chain, so audio must be flowing. The splash enables it ~1 s *asynchronously* — poll until `window.audioEnabled === true` before continuing.
5. **Switch to CW mode:** `window.setMode('CW')`. This is **async** (round-trips to the rig) — poll until `window.currentMode === 'CW'` (a few hundred ms).
6. **Open the CW bar:** `window.renderCWOverlay()` (equivalently the bottom-bar **KEY** button, which calls `toggleCWBar()`). Confirm `#cwBar` no longer has class `hidden`.
7. **Turn the decoder on:** click `#cwDecoderToggle` (the **DECODE** button). It gains class `active` and `textContent` `DECODE`. Do this on **both** tabs (each side decodes the other). Give it ~1 s to spin up its worklet.

When refs go stale across snapshots, drive directly via `browser_evaluate` with stable IDs (the normal path, not a fallback):
- Buttons: `cwDecoderToggle` (DECODE on/off), `cwStopBtn`, `cwCloseBtn`, `cwSpeedUp`, `cwSpeedDown`, `cwLogBtn`
- Fields: `cwInput` (type-to-send), `cwSpeedInput`, `cwCallInput`, `cwQsoInput`
- TX transcript (what THIS station is sending): `#cwDisplay` (and the `cwCharacters` global)
- **RX decode output (what this station COPIED): `#cwDecoderTextInner` `.textContent`** ← assert on this
- Functions (global): `setMode('CW')`, `renderCWOverlay()`, `toggleCWBar()`, `sendCWText(text)`
- State (globals on `window`): `currentMode`, `cwSpeed` (default 20 wpm), `audioEnabled`

## QSO sequence (single bidirectional run)

1. **A → key a string**: on tab A, `window.sendCWText('CQ TEST DE W1AAA')`. This queues `{cmd:'sendCW'}`; virtualrig synthesizes the morse at the rig's CW pitch and routes it to B. At 20 wpm this 16-char string plays in ~8–10 s.
2. **Read B's copy**: on tab B, poll `document.getElementById('cwDecoderTextInner').textContent` until it contains the sent text (allow ~30 s; give a few extra seconds for trailing characters). On the clean virtual channel at 20 wpm the copy is exact (`CQ TEST DE W1AAA`), but **assert tolerantly** — require the callsign `W1AAA` (and ideally `TEST`/`CQ`) to appear, not a byte-exact match, since a real decoder can drop or mangle a character.
3. **B → key the reply**: on tab B, `window.sendCWText('W1AAA DE W1BBB GE')`.
4. **Read A's copy**: on tab A, poll `#cwDecoderTextInner` until it contains `W1BBB`.
5. **Screenshot both** with `browser_take_screenshot`, `fullPage: true`. Save as `.playwright-mcp/wfweb-cw-A.png` and `...-B.png`. (Bare names land in the project root; `/tmp` is rejected — the MCP roots are the project dir and `.playwright-mcp/`.)

## Teardown and report

1. `browser_close` (both tabs), then `./scripts/testrig.sh down`.
2. Report PASS/FAIL and the two screenshot paths. PASS = each side copied the other's transmission (the expected callsign appears in its `#cwDecoderTextInner`).

## Pitfalls — read before debugging

- **Two gates, not one.** The decoder needs (a) `window.audioEnabled === true` (RX audio flowing — enabled ~1 s after CLICK TO START) **and** (b) the **DECODE** toggle (`#cwDecoderToggle`) clicked `active`. Miss either and `#cwDecoderTextInner` stays empty.
- **`setMode('CW')` is async.** Poll `window.currentMode === 'CW'` before keying — keying while still in USB means no CW is generated and the receiver isn't gated to hear it.
- **Both rigs must be in CW on the same freq.** Audio gates on matching freq+mode (same as packet). If one side is still USB, the mixer won't route the morse to it.
- **Don't confuse TX transcript with RX decode.** `#cwDisplay` / `cwCharacters` is the *sender's* outgoing text (what it's keying). The *received* copy is `#cwDecoderTextInner` on the **other** tab. Assert on the receiver's `#cwDecoderTextInner`.
- **Assert tolerantly.** The CW decoder is heuristic. On the clean virtual channel at 20 wpm it's a perfect copy, but write the assert as "contains the callsign", not byte-equality, so a single mis-decode doesn't fail a genuinely-working run. If you want maximum robustness, slow down (`cwSpeed` / `#cwSpeedDown`) before keying.
- **State is window globals**, not a namespaced `window.X.state` object.
- **Don't skip `localStorage.clear()`** — clean-slate mirrors a fresh user and avoids stale CW speed/mode.
- **Stale Playwright `ref=` handles** re-render constantly; drive via `browser_evaluate` + the stable IDs above.
- **Screenshots are sandboxed** to the project dir / `.playwright-mcp/`; `/tmp` is rejected.
- **Never key a physically-connected rig** — this skill is virtual-bench only (no real RF).
