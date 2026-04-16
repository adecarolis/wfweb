#!/bin/bash
# Build RADE custom Opus via MSYS2. (called from build.bat)
set -e
pacman -S --noconfirm --needed autoconf automake libtool make patch gcc cmake > /dev/null 2>&1
RADAE_DIR="$(cygpath "$1")"
cd "$RADAE_DIR"
mkdir -p build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
