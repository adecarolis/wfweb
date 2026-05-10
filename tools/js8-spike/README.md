# JS8 Emscripten encoder spike

One-shot exploratory build to verify that JS8Call-improved's `JS8::encode()`
free function compiles cleanly to WebAssembly and produces correct tone arrays.

## Result

PASS. 14.6 KB .wasm, all 79 output tones in [0,7], all three Costas arrays
match the JS8 ORIGINAL pattern at offsets 0, 36, 72.

## Build (manual, throwaway)

    emcc -std=c++20 -O2 -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 \
        -s EXPORT_NAME=createJS8Encode \
        -s EXPORTED_FUNCTIONS='["_js8_encode","_malloc","_free"]' \
        -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAP32"]' \
        -s ALLOW_MEMORY_GROWTH=1 -s DISABLE_EXCEPTION_CATCHING=0 \
        js8_encode.cpp -o js8_encode.mjs

    node test_encode_raw.mjs

## What's in js8_encode.cpp

Verbatim extraction from JS8Call-improved/JS8_Mode/JS8.cpp:
  - alphabet + alphabetWord  (lines 847..897)
  - parity matrix lambda     (lines 963..1053)
  - encode() free function   (lines 2776..2911)
Plus a hand-rolled CRC-12/0xc06 (replacing boost::augmented_crc, ~20 lines).

No Qt, no Boost, no FFTW, no Eigen — the encoder is genuinely standalone.

## What this means for the real port

- TX side is essentially solved at the codec level. Real work is the audio
  synthesis (8-FSK at 12 kHz) and integration with the wfweb TX path.
- The Boost replacement isn't even necessary in the real port because
  Emscripten ships Boost headers via -sUSE_BOOST_HEADERS=1.
- This spike is meant to be deleted once tools/build-js8-wasm.sh exists.
