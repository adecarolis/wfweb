#!/bin/sh
# Compile the vendored JS8 codec subset to a WASM ES module so the
# browser can encode/decode JS8 frames without the C++ server in the
# loop.
#
# Output: resources/web-standalone/wasm/js8.{mjs,wasm}
#
# Requires: emcc on PATH (any 3.x release).
#
# Scope: encoder + DecoderImpl (covers JS8 Normal/Fast/Turbo/Slow/Ultra
# at the codec level) + Varicode + DecodedText. Audio I/O, Qt
# threading, the QObject Decoder/Worker wrapper, JS8Call's UI, network,
# logbook, and transceiver code are NOT included — the wfweb SPA
# supplies its own Web Worker harness, audio bus, PTT path, and rig
# control. See resources/js8-src/README-vendoring.md.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/resources/js8-src"
OUT_DIR="$REPO_ROOT/resources/web-standalone/wasm"
OUT_MJS="$OUT_DIR/js8.mjs"
FFTW_CACHE="$SRC/.fftw-cache"

if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not on PATH — install Emscripten first." >&2
    echo "  https://emscripten.org/docs/getting_started/downloads.html" >&2
    exit 1
fi

# Emscripten's distro-installed emcc (Debian/Ubuntu) typically has
# FROZEN_CACHE = True in /usr/share/emscripten/.emscripten and a
# root-owned cache dir. That blocks fetching the boost-headers port on
# first use. We work around it by:
#   1. cloning the read-only system cache into a writable per-build-tree
#      cache (sysroot, sysroot_install.stamp, etc — already populated)
#   2. setting EM_CACHE to point at that writable copy
#   3. exporting EM_FROZEN_CACHE=0 so emcc will fetch+add boost on top
#
# Cost: ~50 MB of duplicated cache files, one-time per repo checkout.
EM_CACHE="$REPO_ROOT/resources/js8-src/.emcache"
SYSTEM_CACHE="/usr/share/emscripten/cache"
if [ -d "$SYSTEM_CACHE" ] && [ ! -f "$EM_CACHE/sysroot_install.stamp" ]; then
    echo "tools/build-js8-wasm.sh: cloning system Emscripten cache to $EM_CACHE"
    mkdir -p "$EM_CACHE"
    cp -a "$SYSTEM_CACHE"/. "$EM_CACHE/"
fi
mkdir -p "$EM_CACHE"
export EM_CACHE

# EM_FROZEN_CACHE override doesn't always stick on system-installed emcc
# (it depends on .emscripten config file precedence). Side-step the
# whole question by writing a per-build .emscripten that mirrors the
# system one minus the FROZEN_CACHE line, and pointing emcc at it.
EM_CONFIG_FILE="$REPO_ROOT/resources/js8-src/.emcache/.emscripten"
if [ ! -f "$EM_CONFIG_FILE" ]; then
    grep -v '^FROZEN_CACHE' /usr/share/emscripten/.emscripten \
        > "$EM_CONFIG_FILE" 2>/dev/null || true
    echo "FROZEN_CACHE = False" >> "$EM_CONFIG_FILE"
fi
export EM_CONFIG="$EM_CONFIG_FILE"

# FFTW is built once and cached; bring it up if needed.
if [ ! -f "$FFTW_CACHE/libfftw3f.a" ]; then
    "$REPO_ROOT/tools/build-js8-fftw.sh"
fi

mkdir -p "$OUT_DIR"

emcc -std=c++20 -O3 -fno-rtti \
    -DJS8_WASM=1 \
    -I "$SRC" \
    -I "$SRC/qt-shim" \
    -I "$SRC/vendor" \
    -I "$FFTW_CACHE" \
    -sUSE_BOOST_HEADERS=1 \
    "$SRC/JS8_Mode/JS8.cpp" \
    "$SRC/JS8_Mode/JS8Submode.cpp" \
    "$SRC/JS8_Mode/DecodedText.cpp" \
    "$SRC/JS8_Mode/FrequencyTracker.cpp" \
    "$SRC/JS8_Main/Varicode.cpp" \
    "$SRC/JS8_JSC/JSC.cpp" \
    "$SRC/JS8_JSC/JSC_list.cpp" \
    "$SRC/JS8_JSC/JSC_map.cpp" \
    "$SRC/api/js8_wasm_api.cpp" \
    "$FFTW_CACHE/libfftw3f.a" \
    -lm \
    -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 \
    -s EXPORT_NAME=createJS8 \
    -s EXPORTED_FUNCTIONS='[
        "_js8_encode","_js8_pack",
        "_js8_decoder_new","_js8_decoder_free",
        "_js8_decoder_push","_js8_decoder_run","_js8_decoder_run_modes",
        "_js8_decoder_pop",
        "_js8_free_string","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAP32","HEAPF32"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=33554432 \
    -s ENVIRONMENT=web,worker \
    -s DISABLE_EXCEPTION_CATCHING=0 \
    -o "$OUT_MJS"

echo "tools/build-js8-wasm.sh: done"
echo "  $OUT_MJS  ($(stat -c%s "$OUT_MJS") bytes)"
echo "  $OUT_DIR/js8.wasm ($(stat -c%s "$OUT_DIR/js8.wasm") bytes)"
