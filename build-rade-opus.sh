#!/bin/bash
# Build RADE custom Opus via MSYS2, then patch headers for MSVC. (called from build.bat)
set -e
pacman -S --noconfirm --needed autoconf automake libtool make patch gcc cmake > /dev/null 2>&1
RADAE_DIR="$(cygpath "$1")"
cd "$RADAE_DIR"
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Patch Opus nnet.h: add _MSC_VER guard for RADE_EXPORT
NNET_H="build_opus-prefix/src/build_opus/dnn/nnet.h"
if [ -f "$NNET_H" ] && ! grep -q '_MSC_VER' "$NNET_H"; then
    sed -i 's|#define RADE_EXPORT __attribute__((visibility("default")))|#ifdef _MSC_VER\n    #define RADE_EXPORT\n  #else\n    #define RADE_EXPORT __attribute__((visibility("default")))\n  #endif|' "$NNET_H"
    echo "Patched $NNET_H"
fi

# Patch rade_api.h: add RADE_STATIC guard
RADE_API_H="$RADAE_DIR/src/rade_api.h"
if [ -f "$RADE_API_H" ] && ! grep -q 'RADE_STATIC' "$RADE_API_H"; then
    sed -i 's|#if IS_BUILDING_RADE_API|#if defined(RADE_STATIC)\n#define RADE_EXPORT\n#elif IS_BUILDING_RADE_API|' "$RADE_API_H"
    echo "Patched $RADE_API_H"
fi
