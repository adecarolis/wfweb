# Building wfweb on Windows

## Prerequisites

| Dependency | Version | Location |
|---|---|---|
| Visual Studio 2022 Build Tools | 17.x | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| Qt | 5.15.2 (msvc2019_64) | `C:\Qt\5.15.2\msvc2019_64` |
| vcpkg packages | opus, portaudio, hidapi, openssl | `C:\vcpkg\installed\x64-windows` |

### vcpkg packages

```
vcpkg install opus:x64-windows portaudio:x64-windows hidapi:x64-windows openssl:x64-windows
```

## Build

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

The binary is placed at `wfweb\wfweb.exe` along with DLLs, rigs, and plugin directories.

### What "clean" removes

- `Makefile`, `Makefile.Debug`, `Makefile.Release`, `.qmake.stash`
- `release/`, `debug/` (intermediate object files)
- `wfweb/`, `wfserver-debug/` (output directories)

## Project file

The build uses `wfserver.pro` with:
- `CONFIG+=release` — release build optimizations
- `VCPKG_DIR=C:/vcpkg/installed/x64-windows` — vcpkg dependency path
- `DESTDIR = wfweb` — output directory (set in the .pro file)

## Notes

- The build is **incremental by default** — only modified files are recompiled.
- If you change the `.pro` file, qmake regenerates the Makefiles automatically on next `build.bat` run.
- If you get stale object errors or linker issues, run `build.bat clean` to start fresh.
- The `wfview-release/` and `wfview-debug/` directories are for the GUI app (`wfview.pro`), not the server.
