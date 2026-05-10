# FFTW under Emscripten — spike

Confirms FFTW3 builds cleanly with emcc and produces correct results
in WASM. Mirrors JS8.cpp's call pattern (single-precision `fftwf_*`,
1D forward complex DFT, FFTW_ESTIMATE).

## Build FFTW3 once

    curl -sL https://www.fftw.org/fftw-3.3.10.tar.gz -o fftw.tar.gz
    tar -xzf fftw.tar.gz && cd fftw-3.3.10
    emconfigure ./configure \
        --enable-float \
        --disable-shared --disable-fortran --disable-doc --disable-threads \
        --enable-static \
        --host=i686-linux-gnu             # config.sub doesn't know wasm32-emscripten
    emmake make -j$(nproc)
    # Output: ./.libs/libfftw3f.a

`--enable-float` is critical — JS8.cpp uses `fftwf_*` (single-precision) symbols, not `fftw_*`. Without this flag you'd build the double-precision lib and miss every JS8 symbol.

## Build the test against it

    emcc -O2 -I fftw-3.3.10/api \
        fft_test.c fftw-3.3.10/.libs/libfftw3f.a -lm \
        -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 \
        -s ENVIRONMENT=node -s SINGLE_FILE=1 \
        -o fft_test.js
    node fft_test.js

## Result

    bin  5: 64.00       <- pure tone at bin 5, magnitude = N as expected
    peak bin=5 magnitude=64.00 (expected bin=5, mag~=64)
    exit=0

## Sizes

- libfftw3f.a (static lib): a few MB (most never linked into the final wasm)
- Test wasm with FFTW linked: ~580 KB (includes FFTW codelets for the sizes used)
- Final JS8 wasm will be larger because we link more codelets; budget 1–2 MB.
