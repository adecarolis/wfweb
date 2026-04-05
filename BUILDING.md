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

### Optional: browser WSPR RX

WSPR TX builds as-is. Browser WSPR RX requires a generated decoder bundle, which is intentionally not committed to git.

Generate it before running `qmake` if you want `make install` and package builds to include it:

```bash
sudo apt-get install -y emscripten
bash resources/build-wspr-decoder-wasm.sh
qmake wfweb.pro
make -j$(nproc)
```

If you run the uninstalled binary directly from the source tree, `wfweb` also finds the generated decoder at `resources/web-generated/wspr-decoder-wasm.js` automatically.

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
| vcpkg packages | opus, portaudio, hidapi, openssl | `C:\vcpkg\installed\x64-windows` |

#### vcpkg packages

```
vcpkg install opus:x64-windows portaudio:x64-windows hidapi:x64-windows openssl:x64-windows
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

### Output

The self-contained deployment directory is `wfweb-release\`, containing:
- `wfweb.exe` — the server binary
- Qt runtime DLLs and plugins (deployed via `windeployqt`)
- vcpkg DLLs (portaudio, opus, hidapi, OpenSSL)
- `rigs\` — rig definition files

### What "clean" removes

- `Makefile`, `Makefile.Debug`, `Makefile.Release`, `.qmake.stash`
- `release/`, `debug/` (intermediate object files)
- `wfweb-release/`, `wfweb-debug/` (output directories)

## macOS

### Prerequisites

Install Qt 5 and the required libraries via Homebrew:

```bash
brew install qt@5 portaudio opus openssl@3
```

The build also requires these source trees cloned as sibling directories next to `wfweb/`:

| Directory | Repository / Source |
|---|---|
| `../rtaudio` | https://github.com/thestk/rtaudio |
| `../eigen` | https://gitlab.com/libeigen/eigen |
| `../opus` | https://github.com/xiph/opus (needs `include/` headers) |

(`../r8brain-free-src` is referenced as an include path but no sources are compiled from it — an empty directory is sufficient.)

### Build

Homebrew's Qt 5 is keg-only, so use its full path for `qmake`:

```bash
/opt/homebrew/opt/qt@5/bin/qmake wfweb.pro
make -j$(sysctl -n hw.ncpu)
```

Optional browser WSPR RX also needs a generated decoder bundle:

```bash
brew install emscripten
bash resources/build-wspr-decoder-wasm.sh
/opt/homebrew/opt/qt@5/bin/qmake wfweb.pro
make -j$(sysctl -n hw.ncpu)
```

If you run the uninstalled binary directly from the source tree, `wfweb` also finds the generated decoder at `resources/web-generated/wspr-decoder-wasm.js` automatically.

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
