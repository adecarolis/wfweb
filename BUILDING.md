# Building wfweb

## Linux (Ubuntu/Debian)

### Prerequisites

```bash
sudo apt-get install -y \
    qt5-qmake qtbase5-dev libqt5serialport5-dev \
    qtmultimedia5-dev libqt5websockets5-dev \
    libqt5gamepad5-dev libqt5printsupport5 \
    libopus-dev libeigen3-dev \
    portaudio19-dev librtaudio-dev \
    libhidapi-dev libudev-dev libpulse-dev \
    libqcustomplot-dev \
    openssl
```

### Clone

The repository uses a git submodule for the FT8/FT4 decoder. Make sure to
initialise it when cloning:

```bash
git clone --recursive https://github.com/adecarolis/wfweb.git
```

If you already cloned without `--recursive`:

```bash
git submodule update --init
```

### Build

```bash
qmake wfweb.pro
make -j$(nproc)
```

### Install

```bash
sudo make install
```

This installs the binary, rig files, and a systemd service unit. To start at boot:

```bash
sudo systemctl enable --now wfweb@$USER
```

### Build a .deb package

See `.github/workflows/build.yml` for the full packaging steps.

## Windows

### Prerequisites

| Dependency | Version | Location |
|---|---|---|
| Visual Studio 2022 Build Tools | 17.x | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| Qt | 5.15.2 (msvc2019_64) | `C:\Qt\5.15.2\msvc2019_64` |
| MSYS2 | latest | `C:\msys64` |
| vcpkg packages | portaudio, eigen3, hidapi, openssl | `C:\vcpkg\installed\x64-windows` |

MSYS2 is required to build two dependencies from source (RADE custom Opus and
codec2). The build script installs the needed MSYS2 packages automatically.

#### vcpkg packages

```
vcpkg install eigen3:x64-windows portaudio:x64-windows hidapi:x64-windows openssl:x64-windows
```

Note: `opus` is **not** installed via vcpkg. A custom Opus build with DRED/OSCE
extensions is compiled from the `radae_nopy` submodule and statically linked for
RADE support.

### Clone

```
git clone --recursive https://github.com/adecarolis/wfweb.git
```

If you already cloned without `--recursive`:

```
git submodule update --init
```

#### Sibling repositories

Clone `rtaudio` alongside `wfweb/`:

```
git clone --depth 1 https://github.com/thestk/rtaudio.git ..\rtaudio
```

### Build

Open any terminal (cmd, PowerShell, Git Bash, MSYS2) and run:

```
build.bat              # Incremental release build
build.bat clean        # Clean all artifacts, then rebuild
build.bat cleanonly    # Clean all artifacts without rebuilding
```

From MSYS2/Git Bash/Claude Code, use:
```bash
cmd //c ".\\build.bat"          # runs in background from MSYS perspective
tail -f build.log               # watch progress (in another terminal or after)
```

All build output goes to `build.log`. The last line is `EXIT:0` (success) or `EXIT:1` (failure).

### What `build.bat` does

`build.bat` is self-contained and automates the full build pipeline:

1. **RADE custom Opus** (if `radae_nopy` submodule is present):
   - Runs `cmake` + `make` inside MSYS2 to fetch and build the custom Opus
     source (with LPCNet/FARGAN) via CMake ExternalProject.
   - Patches Opus and RADE headers for MSVC compatibility (PowerShell).
   - Builds the custom Opus as a static `.lib` using MSVC cmake.
   - Skipped if `resources/radae_nopy/build/opus_msvc_build/Release/opus.lib` already exists.

2. **codec2 / FreeDV** (if `codec2.lib` not yet in vcpkg prefix):
   - Clones [drowe67/codec2](https://github.com/drowe67/codec2) into `codec2/`.
   - Builds `libcodec2.dll` using MSYS2 MinGW (codec2 requires GCC due to C99
     features not supported by MSVC).
   - Generates an MSVC import library (`codec2.lib`) from the DLL using
     `gendef` + `lib.exe`.
   - Installs the DLL, import lib, and headers into the vcpkg prefix.
   - Skipped if `codec2.lib` already exists in the vcpkg prefix.

3. **wfweb**: runs `qmake` + `nmake`, deploys Qt runtime via `windeployqt`,
   copies vcpkg DLLs, rig files, and licenses into `wfweb-release\`.

Both dependency builds are incremental — they only run on the first build or
after cleaning.

### Output

The self-contained deployment directory is `wfweb-release\`, containing:
- `wfweb.exe` — the server binary (with RADE statically linked)
- Qt runtime DLLs and plugins (deployed via `windeployqt`)
- `libcodec2.dll` — FreeDV codec2 library
- vcpkg DLLs (portaudio, hidapi, OpenSSL)
- `rigs\` — rig definition files

### What "clean" removes

- `Makefile`, `Makefile.Debug`, `Makefile.Release`, `.qmake.stash`
- `release/`, `debug/` (intermediate object files)
- `wfweb-release/`, `wfweb-debug/` (output directories)

Note: `build.bat clean` does **not** remove the RADE or codec2 build artifacts.
To force a full rebuild of those, delete `resources/radae_nopy\build\opus_msvc_build\` and/or
remove `codec2.lib` from `C:\vcpkg\installed\x64-windows\lib\`.

### Helper scripts

These are called by `build.bat` and are not intended to be run manually:

| Script | Purpose |
|---|---|
| `build-rade-opus.sh` | MSYS2: cmake build of RADE custom Opus |
| `build-codec2.sh` | MSYS2 MinGW: build codec2 as a DLL, install headers |

### Troubleshooting

**RADE build fails immediately**: If the RADE MSYS2 cmake step fails with
`ninja: error: bad $-escape`, delete `resources/radae_nopy/build/` and rebuild.
The `build-rade-opus.sh` script forces the "Unix Makefiles" cmake generator to
avoid this issue.

**RADE MSVC build fails with `RADE_EXPORT` errors**: The header patching step
may have been skipped. Delete `resources/radae_nopy/build/opus_msvc_build/` and
rebuild — the patching step runs independently of the MSYS2 build and will
re-apply the patches.

**Full RADE rebuild**: Delete the entire `resources/radae_nopy/build/` directory.
The next build will redo the MSYS2 cmake, header patching, and MSVC build from
scratch.

## macOS

### Prerequisites

Install Qt 5 and the required libraries via Homebrew:

```bash
brew install qt@5 portaudio opus openssl@3
```

For optional FreeDV and RADE support:

```bash
brew install codec2 cmake autoconf automake libtool
```

`codec2` enables FreeDV modes (700D, 700E, 1600). `cmake`/`autoconf`/`automake`/`libtool` are needed to build the RADE submodule (see below).

The build also requires these source trees cloned as sibling directories next to `wfweb/`:

| Directory | Repository / Source |
|---|---|
| `../rtaudio` | https://github.com/thestk/rtaudio |
| `../eigen` | https://gitlab.com/libeigen/eigen |
| `../opus` | https://github.com/xiph/opus (needs `include/` headers) |

(`../r8brain-free-src` is referenced as an include path but no sources are compiled from it — an empty directory is sufficient.)

### Build RADE (optional)

RADE support requires building the `radae_nopy` submodule. This compiles a
custom Opus (with LPCNet/FARGAN) and the RADE sources, which are then statically
linked into the binary.

```bash
git submodule update --init resources/radae_nopy
cd resources/radae_nopy
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
cd ../..
```

If the submodule is not built, wfweb still compiles — just without the RADE mode.

### Build

Homebrew's Qt 5 is keg-only, so use its full path for `qmake`:

```bash
/opt/homebrew/opt/qt@5/bin/qmake wfweb.pro
make -j$(sysctl -n hw.ncpu)
```

qmake auto-detects both `codec2` (via `pkg-config`) and RADE (via the built
submodule) and prints which features are enabled.

This produces the `wfweb` binary in the project root.

### Run

```bash
./wfweb
```

The web interface is served on `https://localhost:8080` (self-signed certificate).

## Notes

- The project file is `wfweb.pro`.
- Builds are **incremental by default** — only modified files are recompiled.
- If you change the `.pro` file, qmake regenerates the Makefiles automatically on the next build.
- If you get stale object errors or linker issues, clean and rebuild.
- On Windows, `windeployqt` runs automatically to deploy Qt DLLs, making the output directory portable.
