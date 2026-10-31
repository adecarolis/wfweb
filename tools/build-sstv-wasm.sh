#!/bin/sh
# Build the SSTV codec (the `sstv` Rust crate behind a plain C ABI, see
# resources/sstv-shim/) to WebAssembly for both browser builds.
#
# Output: resources/web-standalone/wasm/sstv.wasm  (committed)
#
# Requires: cargo with the wasm32-unknown-unknown target (rustup), and
# wasm-opt from Binaryen (on PATH, or in ~/emsdk/upstream/bin).
#
# Why wasm-opt: the crate's decoder pulls samples from an iterator, and a
# pull cannot wait for the sound card inside WebAssembly. The Asyncify pass
# instruments every function on the call path to the imported
# `sstv_await_samples`, so that import can unwind the Rust stack into a
# buffer, return a Promise to JS, and rewind when more audio has been
# pushed. Only the RX path reaches that import; the encoder is untouched.
# SharedArrayBuffer + Atomics.wait would do the same without Asyncify but
# needs COOP/COEP headers, which GitHub Pages cannot send and which would
# break the APRS map tiles.

set -eu

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SHIM="$REPO_ROOT/resources/sstv-shim"
TARGET_DIR="$REPO_ROOT/build-wasm/sstv"
OUT_DIR="$REPO_ROOT/resources/web-standalone/wasm"
OUT="$OUT_DIR/sstv.wasm"
TARGET="wasm32-unknown-unknown"

if ! command -v cargo >/dev/null 2>&1 && [ -x "$HOME/.cargo/bin/cargo" ]; then
    PATH="$HOME/.cargo/bin:$PATH"
fi
if ! command -v wasm-opt >/dev/null 2>&1 && [ -x "$HOME/emsdk/upstream/bin/wasm-opt" ]; then
    PATH="$HOME/emsdk/upstream/bin:$PATH"
fi
export PATH

if ! command -v cargo >/dev/null 2>&1; then
    echo "ERROR: cargo not found. Install Rust via rustup:" >&2
    echo "  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --target $TARGET" >&2
    exit 1
fi
if ! rustc --print target-list 2>/dev/null | grep -qx "$TARGET" || \
   ! rustc --print sysroot >/dev/null 2>&1 || \
   [ ! -d "$(rustc --print sysroot)/lib/rustlib/$TARGET" ]; then
    echo "ERROR: the $TARGET standard library is not installed." >&2
    echo "  rustup target add $TARGET" >&2
    exit 1
fi
if ! command -v wasm-opt >/dev/null 2>&1; then
    echo "ERROR: wasm-opt (Binaryen) not found; apt install binaryen, or use the emsdk copy." >&2
    exit 1
fi

echo "cargo:    $(cargo --version)"
echo "wasm-opt: $(wasm-opt --version)"

mkdir -p "$TARGET_DIR" "$OUT_DIR"
(
    cd "$SHIM"
    cargo build --release --target "$TARGET" --target-dir "$TARGET_DIR"
)

RAW="$TARGET_DIR/$TARGET/release/wfweb_sstv_shim.wasm"
[ -f "$RAW" ] || { echo "ERROR: $RAW not produced" >&2; exit 1; }

# Asyncify. `asyncify-imports` limits the instrumentation to call paths that
# can reach sstv_await_samples; everything else keeps its plain code. The
# --enable flags are the wasm features Rust's wasm32 target emits by default
# (all baseline in every browser since 2021; no SIMD).
wasm-opt -O3 \
    --enable-bulk-memory --enable-bulk-memory-opt --enable-nontrapping-float-to-int \
    --enable-sign-ext --enable-mutable-globals --enable-multivalue --enable-reference-types \
    --asyncify \
    --pass-arg=asyncify-imports@env.sstv_await_samples \
    "$RAW" -o "$OUT"

echo
echo "Wrote $OUT ($(wc -c < "$OUT") bytes)"
echo "Exports:"
node -e '
const fs = require("fs");
const m = new WebAssembly.Module(fs.readFileSync(process.argv[1]));
const ex = WebAssembly.Module.exports(m).map(e => e.name).filter(n => n.startsWith("sstv_") || n.startsWith("asyncify_"));
const im = WebAssembly.Module.imports(m).map(i => i.module + "." + i.name);
console.log("  " + ex.join(" "));
console.log("Imports:\n  " + im.join(" "));
' "$OUT" 2>/dev/null || true
