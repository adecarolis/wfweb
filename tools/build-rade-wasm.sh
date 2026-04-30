#!/bin/sh
# Compile RADE V1 (radae_nopy) + LPCNet + FARGAN + speex resampler + rade_text
# to a WASM ES module so the browser-only wfweb build can encode/decode RADE
# voice without the C++ server in the loop.
#
# Output: resources/web/wasm/rade.mjs + resources/web/wasm/rade.wasm
#
# Note: NOT SINGLE_FILE — the .wasm payload is ~15-25 MB once weights are
# baked in. Keeping it as a separate file lets the browser cache it across
# loads and lets us lazy-load only when the user picks RADE mode.
#
# Requires: emcc on PATH (any 3.x release).
#
# Scope: rade modem (rade_api_nopy + rade_enc/dec + rade_dsp/ofdm/bpf/acq/tx/rx),
# the LPCNet feature extractor (lpcnet_compute_single_frame_features), the
# FARGAN vocoder (fargan_init/cont/synthesize), the speex resampler used by
# RadeProcessor, and the self-contained rade_text encoder/decoder. Everything
# else from Opus (silk/celt/etc.) tags along inside libopus.a but won't be
# linked because nothing exported references it (LTO/dce strips it).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RADAE_DIR="$REPO_ROOT/resources/radae_nopy"
SRC="$RADAE_DIR/src"
SHIM_DIR="$REPO_ROOT/resources/rade-shim"
# Keep the WASM Opus build cache outside the submodule so it doesn't show
# up as "untracked content" in the parent repo's git status.
WASM_BUILD_DIR="$REPO_ROOT/build-wasm/rade"
OPUS_NATIVE_SRC="$RADAE_DIR/build/build_opus-prefix/src/build_opus"
OPUS_WASM_SRC="$WASM_BUILD_DIR/build_opus"
OPUS_WASM_LIB="$OPUS_WASM_SRC/.libs/libopus.a"
OUT_DIR="$REPO_ROOT/resources/web/wasm"
OUT_MJS="$OUT_DIR/rade.mjs"

if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not on PATH — install Emscripten first." >&2
    echo "  https://emscripten.org/docs/getting_started/downloads.html" >&2
    exit 1
fi

if [ ! -d "$OPUS_NATIVE_SRC" ]; then
    echo "ERROR: Opus source not found at $OPUS_NATIVE_SRC" >&2
    echo "  Build the desktop wfweb first so CMake fetches+patches Opus:" >&2
    echo "    cd $RADAE_DIR/build && cmake -DCMAKE_BUILD_TYPE=Release .. && make" >&2
    exit 1
fi

mkdir -p "$OUT_DIR" "$WASM_BUILD_DIR"

# -----------------------------------------------------------------------------
# Step 1: build libopus.a for WASM (one-shot, cached)
# -----------------------------------------------------------------------------
if [ ! -f "$OPUS_WASM_LIB" ]; then
    echo "==> Building Opus (with FARGAN/LPCNet) for WASM…"
    if [ ! -d "$OPUS_WASM_SRC" ]; then
        echo "    copying patched Opus source tree…"
        cp -a "$OPUS_NATIVE_SRC" "$OPUS_WASM_SRC"
        # Wipe any native build artefacts that came along.
        ( cd "$OPUS_WASM_SRC" && make distclean >/dev/null 2>&1 || true )
    fi

    cd "$OPUS_WASM_SRC"
    if [ ! -f Makefile ]; then
        echo "    emconfigure ./configure …"
        # --host=wasm32 tells autotools we are cross-compiling so it doesn't
        # try to run the freshly-built wasm test binary on the host CPU.
        emconfigure ./configure \
            --host=wasm32-unknown-emscripten \
            --with-pic \
            --enable-osce \
            --enable-dred \
            --disable-shared \
            --disable-doc \
            --disable-extra-programs \
            --disable-asm \
            --disable-rtcd
    fi
    echo "    emmake make -j$(nproc) …"
    emmake make -j"$(nproc)"
    cd "$REPO_ROOT"
fi

if [ ! -f "$OPUS_WASM_LIB" ]; then
    echo "ERROR: Opus WASM build did not produce $OPUS_WASM_LIB" >&2
    exit 1
fi

ls -lh "$OPUS_WASM_LIB" | awk '{print "    libopus.a (wasm): "$5}'

# -----------------------------------------------------------------------------
# Step 2: compile RADE + speex + rade_text + wfweb shim into rade.mjs
# -----------------------------------------------------------------------------
RADE_SOURCES="
    $SRC/rade_api_nopy.c
    $SRC/rade_enc.c
    $SRC/rade_dec.c
    $SRC/rade_enc_data.c
    $SRC/rade_dec_data.c
    $SRC/rade_dsp.c
    $SRC/rade_ofdm.c
    $SRC/rade_bpf.c
    $SRC/rade_acq.c
    $SRC/rade_tx.c
    $SRC/rade_rx.c
    $SRC/kiss_fft.c
    $SRC/kiss_fftr.c
"

SPEEX_SOURCE="$REPO_ROOT/src/audio/resampler/resample.c"

WFWEB_SHIM="$SHIM_DIR/wfweb_rade_wasm.c"
RADE_TEXT_SRC="$REPO_ROOT/src/rade_text.c"

# Symbols cwrap()'d from JS — keep them visible past dead-code-elim.
EXPORTED_FUNCS='[
  "_wfweb_rade_init",
  "_wfweb_rade_close",
  "_wfweb_rade_n_features_in_out",
  "_wfweb_rade_n_tx_out",
  "_wfweb_rade_n_tx_eoo_out",
  "_wfweb_rade_n_eoo_bits",
  "_wfweb_rade_nin",
  "_wfweb_rade_nin_max",
  "_wfweb_rade_lpcnet_frame_size",
  "_wfweb_rade_nb_total_features",
  "_wfweb_rade_nb_features",
  "_wfweb_rade_extract_features",
  "_wfweb_rade_tx",
  "_wfweb_rade_tx_eoo",
  "_wfweb_rade_rx",
  "_wfweb_rade_sync",
  "_wfweb_rade_freq_offset",
  "_wfweb_rade_snr",
  "_wfweb_rade_set_eoo_bits",
  "_wfweb_rade_fargan_reset",
  "_wfweb_rade_fargan_warmup",
  "_wfweb_rade_fargan_synth",
  "_wfweb_rade_text_create",
  "_wfweb_rade_text_destroy",
  "_wfweb_rade_text_encode",
  "_wfweb_rade_text_decode",
  "_wfweb_rade_resampler_init",
  "_wfweb_rade_resampler_destroy",
  "_wfweb_rade_resampler_process",
  "_malloc",
  "_free"
]'

EXPORTED_RUNTIME='[
  "cwrap",
  "ccall",
  "HEAPU8",
  "HEAP16",
  "HEAP32",
  "HEAPF32"
]'

OPUS_INC="$OPUS_WASM_SRC"
OPUS_DNN_INC="$OPUS_WASM_SRC/dnn"
OPUS_CELT_INC="$OPUS_WASM_SRC/celt"
OPUS_INCLUDE_INC="$OPUS_WASM_SRC/include"

emcc \
    -O3 \
    -I"$SRC" \
    -I"$OPUS_INCLUDE_INC" \
    -I"$OPUS_DNN_INC" \
    -I"$OPUS_CELT_INC" \
    -I"$OPUS_INC" \
    -I"$REPO_ROOT/include" \
    -I"$REPO_ROOT/src/audio/resampler" \
    -DIS_BUILDING_RADE_API=1 \
    -DRADE_PYTHON_FREE=1 \
    -DOUTSIDE_SPEEX=1 \
    -DRANDOM_PREFIX=wf \
    -DFLOATING_POINT=1 \
    -DEXPORT= \
    -Wno-unused-parameter -Wno-implicit-function-declaration \
    -Wno-incompatible-pointer-types \
    -s MODULARIZE=1 \
    -s EXPORT_ES6=1 \
    -s ENVIRONMENT=web,worker \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=33554432 \
    -s TOTAL_STACK=2097152 \
    -s EXPORTED_FUNCTIONS="$EXPORTED_FUNCS" \
    -s EXPORTED_RUNTIME_METHODS="$EXPORTED_RUNTIME" \
    -msimd128 \
    -o "$OUT_MJS" \
    $RADE_SOURCES \
    $SPEEX_SOURCE \
    "$RADE_TEXT_SRC" \
    "$WFWEB_SHIM" \
    "$OPUS_WASM_LIB"

echo
echo "Built: $OUT_MJS"
ls -lh "$OUT_MJS" "${OUT_MJS%.mjs}.wasm" 2>/dev/null | awk '{print "  "$NF": "$5}'
