#!/bin/bash
set -euo pipefail

EMSDK_DIR="$HOME/emsdk"
if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    echo "Error: emsdk not found at $EMSDK_DIR"
    echo "Install with: git clone https://github.com/emscripten-core/emsdk.git ~/emsdk && cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest"
    exit 1
fi

source "$EMSDK_DIR/emsdk_env.sh" >/dev/null

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Building WSPR decoder WASM module..."

emcc \
    "$SCRIPT_DIR/web/wspr-decoder-wasm.c" \
    "$SCRIPT_DIR/web/wspr/fano.c" \
    "$SCRIPT_DIR/web/wspr/tab.c" \
    "$SCRIPT_DIR/web/wspr/wsprsim_utils.c" \
    "$SCRIPT_DIR/web/wspr/wsprd_utils.c" \
    "$SCRIPT_DIR/web/wspr/nhash.c" \
    "$SCRIPT_DIR/web/wspr/metric_tables.c" \
    -I "$SCRIPT_DIR/web" \
    -I "$SCRIPT_DIR/web/wspr" \
    -O3 -std=c11 \
    -s WASM=1 \
    -s SINGLE_FILE=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='WsprDecoderModule' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_wfweb_wspr_init","_wfweb_wspr_decode","_wfweb_wspr_clear_hashes","_wfweb_wspr_get_result_count","_wfweb_wspr_get_error","_wfweb_wspr_get_result_freq_mhz","_wfweb_wspr_get_result_snr","_wfweb_wspr_get_result_dt","_wfweb_wspr_get_result_sync","_wfweb_wspr_get_result_drift","_wfweb_wspr_get_result_dbm","_wfweb_wspr_get_result_message","_wfweb_wspr_get_result_callsign","_wfweb_wspr_get_result_grid","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","HEAPF32"]' \
    -o "$SCRIPT_DIR/web/wspr-decoder-wasm.js"

echo "Built: $SCRIPT_DIR/web/wspr-decoder-wasm.js"
echo "File size: $(du -sh "$SCRIPT_DIR/web/wspr-decoder-wasm.js" | cut -f1)"
