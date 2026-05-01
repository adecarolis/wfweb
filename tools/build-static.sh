#!/bin/sh
# Build a standalone static distribution of the wfweb browser frontend.
# The output is a directory containing index.html, the transport / CI-V JS
# modules, the FT8/FT4 module, and the existing client-side helpers
# (CW decoder, ggmorse-wasm, packet UI). It runs without the C++ wfweb
# binary — host it on any HTTPS static host (GitHub Pages, Netlify, …)
# or serve locally with `python3 -m http.server` (localhost is treated as
# a secure context for Web Serial).
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

# Standalone-only files (top-level + transport + civ).
cp "$SRC/index.html" "$DIST/"
cp "$SRC/packet.js"  "$DIST/"
cp "$SRC"/transport/*.js "$DIST/transport/"
cp "$SRC"/civ/*.js       "$DIST/civ/"

# Shared assets (CW decoder family, ggmorse, sprites, models).
cp "$SHARED/digits-sprite.png" "$DIST/"
for f in cw-decoder.js cw-decoder-stft.js cw-decoder-utils.js cw-decoder-worker.js \
         ggmorse-wasm.js; do
    cp "$SHARED/$f" "$DIST/"
done
cp "$SHARED"/models/*.onnx "$DIST/models/"
cp "$SHARED"/digits/*.png  "$DIST/digits/"
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

# Tiny README
cat > "$DIST/README.md" <<'EOF'
# wfweb (standalone static bundle)

This is a fully self-contained browser-only build of the wfweb frontend.
It controls an Icom transceiver directly over Web Serial — no server
required.

## Local testing

```
../tools/serve-static.py 8000 .
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

- Frequency tune, mode + filter, PTT, S-meter (Direct mode)

## What is NOT yet supported

- Audio (RX, mic, FT8/CW/voice). Comes in Phase 2.
- Non-Icom rigs.
EOF

echo
echo "Static bundle written to: $DIST"
echo
echo "  ls $DIST"
ls "$DIST"
echo
echo "Test locally:"
echo "  $REPO_ROOT/tools/serve-static.py 8000 $DIST"
echo "  → open http://localhost:8000 in Chrome/Edge"
