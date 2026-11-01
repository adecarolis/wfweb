---
name: sstv-test
description: Run an end-to-end SSTV picture round-trip between two virtual wfweb rigs. Boots the testrig bench and runs tests/sstv_browser.mjs against it (A sends a Robot 36 colour-bar picture from the panel, B decodes it, files it in the server gallery, a second browser on B sees it arrive and go), or the same on two Standalone virtual rigs. Use when the user asks to test SSTV, send a picture between the rigs, verify the SSTV decoder / panel / gallery, or validate the SSTV TX/RX path. (wfweb-vs-wfweb on the bench, no real radio.)
---

Run an end-to-end **SSTV picture round-trip** between two virtual rigs. Unlike the other mode skills this one does not drive Playwright MCP step by step: `tests/sstv_browser.mjs` is the scripted QSO (playwright-core + a local Chromium), so the skill boots the bench, runs it, reads the verdict and the screenshots, tears down. Bench-only (virtual rigs, no hardware, no real RF).

## Prerequisites (verify before starting; fix and stop if missing)

1. `./wfweb` and `./tools/virtualrig/virtualrig` are built. If not: `qmake wfweb.pro && make -j$(nproc)` and `cd tools/virtualrig && qmake && make -j$(nproc)`.
2. `node tests/sstv_browser.mjs --probe` exits 0 (it prints the playwright-core and Chromium it found; exit 3 = none, then `npx playwright install chromium` or set `WFWEB_CHROMIUM`).

## The run

Standalone (no server, the browser air bus between two tabs; ~70 s):

```
WFWEB_SHOT_DIR=.playwright-mcp node tests/sstv_browser.mjs
```

C++ bench (two wfweb servers on two virtual rigs, the gallery on each server, a second browser on B watching it; ~80 s):

```
./scripts/testrig.sh up 2
WFWEB_SSTV_URLS="https://127.0.0.1:9080 https://127.0.0.1:9090" WFWEB_SHOT_DIR=.playwright-mcp node tests/sstv_browser.mjs
./scripts/testrig.sh down
```

Both rigs come up on 14.074.000 USB; the panel keeps a voice mode, so they stay matched and the bench gates the audio through. The bench keeps its pictures in `.testrig/sstv_<i>/` (`--sstv-dir`), never in the operator's data dir.

What the script checks: both panels open from the page and list the worker's 18 modes; the mic level sits at `DIGI_MOD_LEVEL` while open and goes back on close; A's loaded picture fills its card and lands in the gallery's LOADED section (kind `tx`), and the GALLERY header button shows the gallery alone with RECEIVED and LOADED apart; A's SEND keys one digital over bracketed with `enableMic`, with the sweep line crossing the TX picture; B reads the VIS as Robot 36, paints 320x240, and the picture matches what A sent (mean abs error < 25; a miss dumps B's raw 48 kHz PCM next to the screenshots as `sstv-B-rx-48k.pcm` for a post-mortem); the gallery entry carries mode / time / size; in the viewer TO TX loads the received picture to send without filing it again, PIP draws it in the top-right corner of the TX picture and the PIP button walks the corners then takes it off; on a second picture B drops the decode after a few lines and presses START under AUTO: the worker locks on the sync pulses, names Robot 36 from their spacing and the picture holds every line from the lock on (shifted up), then the decoder is back on AUTO; DELETE takes two presses (no `confirm()`) and removes the picture; on the bench the second browser sees the picture arrive and go (`sstvGallery` broadcast).

## Report

PASS/FAIL from the script's last line, the error figure from the "matches what A sent" line, and the two screenshots (`sstv-A.png`, `sstv-B.png` in `WFWEB_SHOT_DIR`). A picture that starts right and turns to garbage (swapped colours, then wrong brightness) means a timing jump in the audio, a frame dropped or inserted on the way (the bench's TX underrun put a 20 ms silent frame in the air once): the vendored crate's re-lock patch recovers after a few rows, so more than a handful of bad rows is a regression there. Replay the dumped PCM through the real worker under node (the shim in `tests/sstv_codec_sim.mjs` loads it; a 20 ms Goertzel sync-pulse timing shows the jump).
