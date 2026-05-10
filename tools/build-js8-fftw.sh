#!/bin/sh
# Build a single-precision FFTW3 static library for Emscripten and cache
# it under resources/js8-src/.fftw-cache/. Idempotent: a second run
# detects the cached artifact and exits immediately. The full build
# takes ~2 minutes; the cache means CI and dev rebuilds skip that.
#
# Inputs:  none
# Outputs: resources/js8-src/.fftw-cache/libfftw3f.a
#          resources/js8-src/.fftw-cache/fftw3.h
#
# Requires: emcc on PATH, curl, tar.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$REPO_ROOT/resources/js8-src/.fftw-cache"
FFTW_VERSION=3.3.10
FFTW_TARBALL_URL="https://www.fftw.org/fftw-${FFTW_VERSION}.tar.gz"

mkdir -p "$CACHE"

if [ -f "$CACHE/libfftw3f.a" ] && [ -f "$CACHE/fftw3.h" ]; then
    echo "tools/build-js8-fftw.sh: already built (delete $CACHE to rebuild)"
    exit 0
fi

if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not on PATH — install Emscripten first." >&2
    echo "  https://emscripten.org/docs/getting_started/downloads.html" >&2
    exit 1
fi

cd "$CACHE"

if [ ! -d "fftw-${FFTW_VERSION}" ]; then
    echo "tools/build-js8-fftw.sh: fetching FFTW ${FFTW_VERSION}"
    curl -sL "$FFTW_TARBALL_URL" -o "fftw.tar.gz"
    tar -xzf fftw.tar.gz
    rm -f fftw.tar.gz
fi

cd "fftw-${FFTW_VERSION}"

# --enable-float    : single-precision (fftwf_*) — what JS8.cpp uses.
# --host=...        : FFTW's config.sub doesn't recognise "wasm32-unknown-
#                     emscripten", so we lie about the host triple.
#                     emconfigure injects the cross compiler regardless.
# disable-shared    : we link statically into the WASM module.
# disable-fortran   : no fortran in our build chain.
# disable-doc       : no docs needed.
# disable-threads   : single-threaded, browser environment.
# enable-static     : produce libfftw3f.a.

if [ ! -f Makefile ]; then
    echo "tools/build-js8-fftw.sh: configuring"
    emconfigure ./configure \
        --enable-float \
        --disable-shared \
        --disable-fortran \
        --disable-doc \
        --disable-threads \
        --enable-static \
        --host=i686-linux-gnu \
        > "$CACHE/configure.log" 2>&1
fi

echo "tools/build-js8-fftw.sh: building (~2 min)"
emmake make -j"$(nproc 2>/dev/null || echo 4)" \
    > "$CACHE/make.log" 2>&1

cp .libs/libfftw3f.a "$CACHE/libfftw3f.a"
cp api/fftw3.h        "$CACHE/fftw3.h"

echo "tools/build-js8-fftw.sh: done"
echo "  $CACHE/libfftw3f.a  ($(stat -c%s "$CACHE/libfftw3f.a") bytes)"
echo "  $CACHE/fftw3.h"
