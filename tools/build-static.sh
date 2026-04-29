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
SRC="$REPO_ROOT/resources/web"
FT8="$REPO_ROOT/resources/ft8ts/dist"
DIST="${1:-$REPO_ROOT/dist}"

if [ ! -d "$SRC" ]; then
    echo "ERROR: source dir $SRC does not exist" >&2
    exit 1
fi

# Make output dir
rm -rf "$DIST"
mkdir -p "$DIST/transport" "$DIST/civ" "$DIST/models" "$DIST/digits"

# Top-level files
cp "$SRC/index.html"           "$DIST/"
cp "$SRC/digits-sprite.png"    "$DIST/"
for f in cw-decoder.js cw-decoder-stft.js cw-decoder-utils.js cw-decoder-worker.js \
         ggmorse-wasm.js packet.js; do
    cp "$SRC/$f" "$DIST/"
done

# Subdirs
cp "$SRC"/transport/*.js "$DIST/transport/"
cp "$SRC"/civ/*.js       "$DIST/civ/"
cp "$SRC"/models/*.onnx  "$DIST/models/"
cp "$SRC"/digits/*.png   "$DIST/digits/"

# FT8/FT4 module + sourcemap (the index.html imports it from /ft8ts.mjs)
cp "$FT8/ft8ts.mjs"     "$DIST/"
cp "$FT8/ft8ts.mjs.map" "$DIST/"

# Inject the WFWEB_STANDALONE flag so the SPA defaults to Direct mode.
# We splice a tiny <script> into the very first <head> opener so it runs
# before everything else. The flag tells the SPA: there is no C++ server
# behind this page; default to talking to the rig directly via Web Serial.
TMP="$DIST/index.html.tmp"
awk '
    !inserted && /<head[^a-zA-Z]/ {
        print
        print "<script>window.WFWEB_STANDALONE = true;</script>"
        inserted = 1
        next
    }
    { print }
' "$DIST/index.html" > "$TMP"
mv "$TMP" "$DIST/index.html"

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
