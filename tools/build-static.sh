#!/bin/sh
# Build wfweb Standalone — the static, browser-only build of the wfweb
# frontend. The output is a directory containing index.html, the
# transport / CI-V JS modules, the FT8/FT4 module, and the client-side
# helpers (CW decoder, ggmorse-wasm, packet UI). It runs without the
# wfweb Server binary — host it on any HTTPS static host (GitHub Pages,
# Netlify, …) or serve locally with `python3 -m http.server` (localhost
# is treated as a secure context for Web Serial).
#
# Usage:
#   tools/build-static.sh [output-dir]
#
# Default output: ./dist

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/resources/web-standalone"
SHARED="$REPO_ROOT/resources/web-shared"
FT8="$REPO_ROOT/resources/ft8ts/dist"
DIST="${1:-$REPO_ROOT/dist}"

if [ ! -d "$SRC" ]; then
    echo "ERROR: source dir $SRC does not exist" >&2
    exit 1
fi
if [ ! -d "$SHARED" ]; then
    echo "ERROR: shared dir $SHARED does not exist" >&2
    exit 1
fi

# Make / refresh output dir. Clear contents in place rather than rm -rf
# the directory itself — a long-running serve-static.py that chdir'd in
# would otherwise be left with a "(deleted)" cwd and refuse all requests.
mkdir -p "$DIST"
find "$DIST" -mindepth 1 -delete
mkdir -p "$DIST/transport" "$DIST/civ" "$DIST/models" "$DIST/digits" "$DIST/wasm"

# Standalone-only files (index + serial transport + CI-V).
cp "$SRC/index.html"     "$DIST/"
cp "$SRC/favicon.svg"    "$DIST/"
cp "$SRC"/transport/*.js "$DIST/transport/"
cp "$SRC"/civ/*.js       "$DIST/civ/"

# Shared assets (CW decoder family, ggmorse, sprites, models, packet UI,
# rig-transport base class).
cp "$SHARED/digits-sprite.png" "$DIST/"
for f in cw-decoder.js cw-decoder-stft.js cw-decoder-utils.js cw-decoder-worker.js \
         ggmorse-wasm.js packet.js reporters.js airbus.js; do
    cp "$SHARED/$f" "$DIST/"
done
cp "$SHARED"/transport/*.js "$DIST/transport/"
cp "$SHARED"/models/*.onnx  "$DIST/models/"
cp "$SHARED"/digits/*.png   "$DIST/digits/"
# WASM modem(s) — only included if already built. tools/build-direwolf-wasm.sh
# is a separate one-off step (requires Emscripten); a missing dist/wasm/* is
# treated as "this build doesn't include packet" rather than a hard error.
if ls "$SRC"/wasm/*.mjs >/dev/null 2>&1; then
    cp "$SRC"/wasm/*.mjs   "$DIST/wasm/"
fi
if ls "$SRC"/wasm/*.html >/dev/null 2>&1; then
    cp "$SRC"/wasm/*.html  "$DIST/wasm/"
fi
if ls "$SRC"/wasm/*-modem.js >/dev/null 2>&1; then
    cp "$SRC"/wasm/*-modem.js "$DIST/wasm/"
fi
# RADE emits a separate .wasm payload (not SINGLE_FILE) so the browser can
# cache the 10 MB modem binary independently from the loader.
if ls "$SRC"/wasm/*.wasm >/dev/null 2>&1; then
    cp "$SRC"/wasm/*.wasm "$DIST/wasm/"
fi

# FT8/FT4 module + sourcemap (the index.html imports it from /ft8ts.mjs)
cp "$FT8/ft8ts.mjs"     "$DIST/"
cp "$FT8/ft8ts.mjs.map" "$DIST/"

# Optional: GitHub Pages custom-domain marker. Drop a CNAME file into
# resources/web-standalone/ once you've picked the domain, and every build
# (CI included) will publish with it.
if [ -f "$SRC/CNAME" ]; then
    cp "$SRC/CNAME" "$DIST/"
fi

# Tiny README
cat > "$DIST/README.md" <<'EOF'
# wfweb Standalone

This is the fully self-contained browser-only build of the wfweb
frontend. It controls an Icom transceiver directly over Web Serial — no
server process required. (For LAN-attached rigs, FreeDV 700D/700E/1600,
or unattended-server features, use **wfweb Server** instead.)

## Local testing

```
../tools/serve-static.py
```

Open `http://localhost:8000` in Chrome or Edge. Localhost is a secure
context, so Web Serial works without HTTPS. The bundled serve-static.py
sends `Cache-Control: no-store` to avoid browser-cache surprises during
iterative development. Plain `python3 -m http.server` works too but
caches aggressively.

Click the dot in the top-right corner → **Connect rig**. Pick the rig's
USB serial port from the OS picker. wfweb auto-probes common baud rates
(19200, 115200, 9600, …) and identifies the rig from its CI-V address;
no manual selection needed.

Subsequent visits auto-reconnect to the same port without a picker.

## Public hosting

Drop this directory onto any HTTPS static host (GitHub Pages, Netlify,
Cloudflare Pages, …). HTTPS is required by the Web Serial API on
non-localhost origins.

## Browser support

Web Serial is implemented in Chromium-family browsers (Chrome, Edge,
Opera, Brave, Arc). Firefox / Safari users can't use this build.

## What works in this build

- Full CI-V control: VFOs, mode + filter, PTT, gains, scope / waterfall,
  memories, P.AMP / ATT, AGC, NB / NR / NOTCH, MON, …
- RX and TX audio (mic via getUserMedia, RX via PCM stream from the rig)
- FT8 / FT4 decode and reply
- CW decode (ggmorse + Goertzel) and CW keying
- AX.25 packet — 300 / 1200 / 9600 baud — including APRS heard-stations,
  beacon scheduler, connected-mode terminal, and YAPP file transfer
- RADE V1 digital voice (browser-side WebAssembly modem)

## Use the Server build for

- LAN-attached rigs (IC-7300 Mk2, IC-9700, IC-7610 LAN, …)
- Classic FreeDV — 700D, 700E, 1600
- PSK Reporter / FreeDV Reporter spotting
- Multi-user, headless, or unattended deployments
- Non-Chromium browsers (Firefox, Safari, …)
EOF

# Cache-bust every local asset URL with ?v=<sha>. WFWEB_BUILD_VERSION lets
# CI override with a clean commit SHA (see .github/workflows/pages.yml). For
# local builds we always tack on a timestamp so iterative rebuilds bust the
# cache even when HEAD hasn't moved.
if [ -z "${WFWEB_BUILD_VERSION:-}" ]; then
    SHA="$(git -C "$REPO_ROOT" rev-parse --short=12 HEAD 2>/dev/null || echo dev)"
    WFWEB_BUILD_VERSION="${SHA}-$(date -u +%s)"
fi
"$REPO_ROOT/tools/fingerprint-static.py" "$DIST" "$WFWEB_BUILD_VERSION"

echo
echo "Static bundle written to: $DIST"
echo
echo "  ls $DIST"
ls "$DIST"
echo
echo "Test locally:"
echo "  $REPO_ROOT/tools/serve-static.py"
echo "  → open http://localhost:8000 in Chrome/Edge"
