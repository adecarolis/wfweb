#!/bin/bash
# Build codec2 as a shared library using MSYS2 MinGW, then create MSVC import lib.
# Called from build.bat: build-codec2.sh <srcdir> <vcpkg_win_path>
set -e

SRCDIR="$(cygpath "$1")"
VCPKG="$(cygpath "$2")"

pacman -S --noconfirm --needed mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-tools-git > /dev/null 2>&1

export PATH="/mingw64/bin:$PATH"
export MSYSTEM=MINGW64

cd "$SRCDIR"
mkdir -p build_mingw && cd build_mingw
/mingw64/bin/cmake -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DUNITTEST=OFF \
    -DCMAKE_C_COMPILER=/mingw64/bin/gcc.exe \
    -DCMAKE_MAKE_PROGRAM=/mingw64/bin/mingw32-make.exe \
    ..
/mingw64/bin/mingw32-make -j$(nproc) codec2

# Copy DLL into vcpkg bin
cp src/libcodec2.dll "$VCPKG/bin/"

# Generate .def from DLL exports
/mingw64/bin/gendef src/libcodec2.dll

# Install headers
mkdir -p "$VCPKG/include/codec2"
for h in codec2.h codec2_fdmdv.h codec2_fifo.h codec2_cohpsk.h codec2_fm.h \
         codec2_ofdm.h freedv_api.h reliable_text.h comp.h modem_stats.h; do
    cp "../src/$h" "$VCPKG/include/codec2/"
done
cp codec2/version.h "$VCPKG/include/codec2/"

# Leave .def for build.bat to create MSVC import lib (needs vcvars64 environment)
cp libcodec2.def "$SRCDIR/"

echo "codec2 MinGW build done"
