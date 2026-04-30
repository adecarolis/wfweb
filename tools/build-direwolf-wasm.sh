#!/bin/sh
# Compile the vendored Direwolf modem subset to a WASM ES module so the
# browser-only wfweb build can demodulate/encode AX.25 packets without the
# C++ server in the loop.
#
# Output: resources/web/wasm/direwolf.mjs
#
# Requires: emcc on PATH (any 3.x release).
#
# Scope: AFSK 300/1200 + G3RUH 9600 modem, HDLC framing, AX.25 packet
# encode/decode.  ax25_link.c (connected-mode), xid.c, dlq.c, FX.25, IL2P,
# and the audio/PTT/IGate/digipeater layers are intentionally NOT included
# — wfweb supplies its own audio bus and the JS side runs the link state
# machine.  See resources/direwolf/README-vendoring.md.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/resources/direwolf"
OUT_DIR="$REPO_ROOT/resources/web/wasm"
OUT_MJS="$OUT_DIR/direwolf.mjs"

if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not on PATH — install Emscripten first." >&2
    echo "  https://emscripten.org/docs/getting_started/downloads.html" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# Mirror the C sources wfweb.pro pulls in (line 274+), minus the
# connected-mode pieces we don't need on the modem-only WASM build.
SOURCES="
    $SRC/src/ax25_pad.c
    $SRC/src/ax25_pad2.c
    $SRC/src/demod.c
    $SRC/src/demod_afsk.c
    $SRC/src/demod_9600.c
    $SRC/src/multi_modem.c
    $SRC/src/hdlc_rec.c
    $SRC/src/hdlc_rec2.c
    $SRC/src/hdlc_send.c
    $SRC/src/gen_tone.c
    $SRC/src/fcs_calc.c
    $SRC/src/dsp.c
    $SRC/src/dtime_now.c
    $SRC/src/rrbb.c
    $SRC/wfweb_direwolf_wasm.c
"

# Symbols cwrap()'d from JS — keep them visible past dead-code-elim.
EXPORTED_FUNCS='[
  "_wfweb_dw_init",
  "_wfweb_dw_baud",
  "_wfweb_dw_process_samples",
  "_wfweb_dw_tx_frame",
  "_wfweb_dw_tx_buffer_ptr",
  "_wfweb_dw_tx_buffer_len",
  "_wfweb_dw_tx_buffer_reset",
  "_malloc",
  "_free"
]'

EXPORTED_RUNTIME='[
  "cwrap",
  "ccall",
  "HEAPU8",
  "HEAP16",
  "HEAP32"
]'

emcc \
    -O3 \
    -I"$SRC/src" -I"$SRC" \
    -DMAJOR_VERSION=1 -DMINOR_VERSION=7 -DEXTRA_VERSION='"wfweb-wasm"' \
    -DHAVE_STRLCPY=1 -DHAVE_STRLCAT=1 \
    -Wno-unused-parameter -Wno-implicit-fallthrough -Wno-write-strings \
    -Wno-sign-compare -Wno-old-style-declaration -Wno-type-limits \
    -s MODULARIZE=1 \
    -s EXPORT_ES6=1 \
    -s ENVIRONMENT=web,worker \
    -s SINGLE_FILE=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=16777216 \
    -s EXPORTED_FUNCTIONS="$EXPORTED_FUNCS" \
    -s EXPORTED_RUNTIME_METHODS="$EXPORTED_RUNTIME" \
    -o "$OUT_MJS" \
    $SOURCES

echo
echo "Built: $OUT_MJS"
ls -lh "$OUT_MJS" | awk '{print "  size: "$5}'
