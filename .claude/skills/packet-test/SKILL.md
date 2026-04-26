---
name: packet-test
description: Run an end-to-end AX.25 packet QSO test between two virtual wfweb rigs at 300 bd and 1200 bd. Boots the testrig bench, drives both browsers via Playwright MCP, exchanges connected-mode frames, screenshots both sides, and tears down. Use when the user asks to test packet, run a packet QSO, verify AX.25, or validate the Direwolf modem against a real terminal session. (9600 G3RUH is intentionally NOT covered.)
---

Run an end-to-end AX.25 packet QSO test between two virtual rigs at **300 bd** and **1200 bd**, drive both browsers via Playwright MCP, screenshot both sides, and tear down. (9600 G3RUH is intentionally NOT tested by this skill.)

## Prerequisites (verify before starting; fix and stop if missing)

1. `./wfweb` and `./tools/virtualrig/virtualrig` are built. If not, build with `qmake wfweb.pro && make -j$(nproc)` and `cd tools/virtualrig && qmake && make -j$(nproc)`.
2. Repo `.mcp.json` includes `--ignore-https-errors` for the playwright MCP. wfweb's cert is self-signed; without this flag chromium refuses with `ERR_CERT_AUTHORITY_INVALID`. Verify the running MCP has the flag: `pgrep -af @playwright/mcp` must show `--ignore-https-errors`. If `.mcp.json` was just edited, the user needs to restart Claude Code (not just `/mcp` reconnect — that re-uses cached args). Stop and tell them.
3. The playwright tools live behind ToolSearch — load them with `select:mcp__playwright__browser_navigate,mcp__playwright__browser_snapshot,mcp__playwright__browser_click,mcp__playwright__browser_type,mcp__playwright__browser_take_screenshot,mcp__playwright__browser_wait_for,mcp__playwright__browser_evaluate,mcp__playwright__browser_close,mcp__playwright__browser_tabs,mcp__playwright__browser_select_option`.

## Boot the bench

```
./scripts/testrig.sh up 2
```

Spawns virtualrig + 2 wfweb daemons. Both rigs come up on **14.074.000 USB** by default — same freq, no `--broadcast` needed (audio-gating on matching freq+mode is what real packet does).

- A → https://127.0.0.1:9080  (virtual-IC7300-A)
- B → https://127.0.0.1:9090  (virtual-IC7300-B)

## Per-tab UI bring-up (do for both A and B)

The Packet panel lives inside the MODE overlay — there is no top-level Packet button.

1. `browser_navigate` to the rig URL.
2. Click the **CLICK TO START** splash button (browsers refuse `AudioContext` without a user gesture).
3. Click **MODE**, then click **Packet** in the mode grid. Rig stays on USB (Packet IS USB on HF).
4. Click the gear (`#packetSettingsBtn`), type the callsign in the **Callsign** input, click **Save**.
   - A: `W1AAA`, B: `W1BBB`. Format must be valid amateur (letter+digit+letters). The single callsign is shared with CW/FT8/FreeDV/APRS — do not add per-mode callsigns.
5. Click the baud button for this iteration (`#packetMode300` or `#packetMode1200`).
6. Click the **TERMINAL** tab.
7. On A only: clear `#termPeerCall` and type the other rig's callsign (`W1BBB`).

When refs go stale across snapshots, drive directly via `browser_evaluate` with stable IDs:
- Buttons: `packetSettingsBtn`, `packetMode300`, `packetMode1200`, `packetClearBtn`, `termConnectBtn`, `termDisconnectBtn`, `termSendBtn`
- Fields: `termPeerCall`, `termInput`
- State: `document.getElementById('termStateChip').textContent` (`DISCONNECTED` / `CONNECTING` / `CONNECTED` / `DISCONNECTING`)
- Object: `window.Packet.state.{mode, terminal.sessions.t1.{state, scrollback, ownCall, peerCall}}`

## QSO sequence (run for each baud: 300, then 1200)

1. **A → Connect**: click `#termConnectBtn`. Poll `termStateChip.textContent === 'CONNECTED'` (a few seconds at 300, faster at 1200). Verify B also flips to `CONNECTED` and B's session scrollback gets `*** CONNECTED to W1AAA`.
2. **A → "Hello"**: set `termInput.value = 'Hello'`, click `#termSendBtn`. Wait on B for `Hello` to render.
3. **B → "How are you"**: same pattern. Wait on A for the text.
4. **Screenshot both** with `browser_take_screenshot`, `fullPage: true`. Save under `.playwright-mcp/wfweb-packet-<baud>-A.png` and `...-B.png`. **Do not use `/tmp`** — it's outside the MCP's allowed roots and will fail.
5. **B → Disconnect**: click `#termDisconnectBtn`. Both chips go to `DISCONNECTED`.
6. Before 1200 bd: click `#packetMode1200` on both tabs, re-set `#termPeerCall` on A (it may revert to placeholder), and optionally wipe `window.Packet.state.terminal.sessions.t1.scrollback = []` then call `window.Packet.renderTerm()` for a clean screenshot.

## Teardown and report

1. `browser_close`, then `./scripts/testrig.sh down`.
2. Report PASS/FAIL per baud and the four screenshot paths (A and B at 300, A and B at 1200).

## Pitfalls — read before debugging

- **Don't poll `browser_wait_for` text 'DISCONNECTED'** — there is a hidden `<span id="digiReporterStatus">disconnected</span>` that matches case-insensitively and never goes visible. Read `termStateChip.textContent` via `browser_evaluate` instead.
- **Don't use `curl https://127.0.0.1:*` to probe** — wfweb's self-signed cert needs a separate exception. Drive everything through the browser.
- **Killing the playwright MCP (`pkill -f @playwright/mcp`) does not auto-respawn it.** The user has to run `/mcp` to reconnect. Only do this if the MCP is genuinely stuck.
- **Both rigs share one callsign field** — do not try to set per-mode callsigns. Setting it once in PKT settings propagates to CW/FT8/FreeDV/APRS, which is by design.
- **Screenshot saves are sandboxed.** Allowed roots are the project dir and `.playwright-mcp/`; `/tmp` is rejected.
