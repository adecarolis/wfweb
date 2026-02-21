#!/bin/bash
set -e

# Source Emscripten SDK
EMSDK_DIR="$HOME/emsdk"
if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    echo "Error: emsdk not found at $EMSDK_DIR"
    echo "Install with: git clone https://github.com/emscripten-core/emsdk.git ~/emsdk && cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest"
    exit 1
fi

source "$EMSDK_DIR/emsdk_env.sh"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/.."

echo "Building ggmorse WASM module..."

emcc \
    "$SCRIPT_DIR/web/ggmorse-wasm.cpp" \
    "$SCRIPT_DIR/ggmorse/src/ggmorse.cpp" \
    "$SCRIPT_DIR/ggmorse/src/resampler.cpp" \
    -I "$SCRIPT_DIR/ggmorse/include" \
    -O2 -std=c++17 \
    -s WASM=1 \
    -s SINGLE_FILE=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='GGMorseModule' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_ggmorse_init","_ggmorse_queue","_ggmorse_decode","_ggmorse_get_text","_ggmorse_get_frequency","_ggmorse_get_speed","_ggmorse_reset","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","HEAPF32"]' \
    -o "$SCRIPT_DIR/web/ggmorse-wasm.js"

echo "Built: $SCRIPT_DIR/web/ggmorse-wasm.js"
echo "File size: $(du -sh "$SCRIPT_DIR/web/ggmorse-wasm.js" | cut -f1)"
