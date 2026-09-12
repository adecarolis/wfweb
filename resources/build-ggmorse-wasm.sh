#!/bin/bash
set -e

# Source Emscripten SDK
EMSDK_DIR="$HOME/emsdk"
if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    echo "Error: emsdk not found at $EMSDK_DIR"
    echo "Install with: git clone https://github.com/emscripten-core/emsdk.git ~/emsdk && cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest"
    exit 1
fi

source "$EMSDK_DIR/emsdk_env.sh" >/dev/null

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/.."

# Upstream ggmorse (https://github.com/ggerganov/ggmorse), pinned by commit so
# the WASM is reproducible. Not a submodule: only this script needs the source.
# Bump GGMORSE_COMMIT to pick up upstream changes, then rebuild and commit the
# resulting ggmorse-wasm.js. Set GGMORSE_SRC to build from a local checkout.
GGMORSE_REPO="https://github.com/ggerganov/ggmorse.git"
GGMORSE_COMMIT="7b4822a8cfdbb1addfe497f3ae8186f142a4ee79"   # 2026-08-24

if [ -z "$GGMORSE_SRC" ]; then
    GGMORSE_SRC="$(mktemp -d)"
    trap 'rm -rf "$GGMORSE_SRC"' EXIT
    echo "Fetching ggmorse @ ${GGMORSE_COMMIT:0:12}..."
    git -C "$GGMORSE_SRC" init -q
    git -C "$GGMORSE_SRC" fetch -q --depth 1 "$GGMORSE_REPO" "$GGMORSE_COMMIT"
    git -C "$GGMORSE_SRC" checkout -q FETCH_HEAD
fi
if [ ! -f "$GGMORSE_SRC/src/ggmorse.cpp" ]; then
    echo "Error: no ggmorse source at $GGMORSE_SRC"
    exit 1
fi

echo "Building ggmorse WASM module..."

emcc \
    "$SCRIPT_DIR/ggmorse-src/ggmorse-wasm.cpp" \
    "$GGMORSE_SRC/src/ggmorse.cpp" \
    "$GGMORSE_SRC/src/resampler.cpp" \
    -I "$GGMORSE_SRC/include" \
    -O2 -std=c++17 \
    -s WASM=1 \
    -s SINGLE_FILE=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='GGMorseModule' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_ggmorse_init","_ggmorse_queue","_ggmorse_decode","_ggmorse_get_text","_ggmorse_get_frequency","_ggmorse_get_speed","_ggmorse_reset","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","HEAPF32"]' \
    -o "$SCRIPT_DIR/web-shared/ggmorse-wasm.js"

echo "Built: $SCRIPT_DIR/web-shared/ggmorse-wasm.js"
echo "File size: $(du -sh "$SCRIPT_DIR/web-shared/ggmorse-wasm.js" | cut -f1)"
