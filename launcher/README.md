# wfweb launcher (Windows)

`wfweb-launcher.exe` is a small Qt Widgets window that sits next to
`wfweb.exe` in the Windows zip. It asks for the things the web UI cannot set
— USB serial port or LAN rig, audio device, CI-V address, web port, settings
file — turns them into the same command-line flags you would type by hand,
runs `wfweb.exe` as a child process, follows its log, and opens the browser
once the server answers. Everything it does can be done from a console with
the flags shown at the top of its log pane; the server binary is unchanged.

## How it talks to the server

- **Flags, not settings edits.** The form is stored under the launcher's own
  settings and becomes `--serial-port`, `--lan`, `--audio-device`, `-p`, `-n`,
  `-s`, … on every start. wfweb does not persist CLI overrides, so its own
  settings (registry, or the file named in "Settings file") are left as they
  are.
- **Log via file.** The server block-buffers its stdout when it is a pipe, but
  flushes the `-l` log file after every line, so the launcher passes
  `-l %LOCALAPPDATA%\wfweb\launcher\wfweb-server.log` and tails that.
- **Up via REST.** A failed port bind does not make the server exit, so the
  launcher polls `GET /api/v1/radio/info` on the REST port (web port + 1 with
  HTTPS, the web port itself without) until it answers, then keeps polling for
  the rig model.
- **Port already in use.** Before starting, the launcher connects to the web
  port. If something answers it looks up the listening process; when that is
  another `wfweb.exe` the dialog shows its command line and offers to stop it
  (a hard stop, so a LAN instance gets a warning about the skipped logout).
  Anything that is not wfweb is left alone.
- **Stop via `q`.** The server's keyboard thread reads stdin even when it is a
  pipe; a `q` runs the same clean shutdown as on the terminal status page. If
  it has not exited after 10 s the launcher kills it. On Windows the child is
  also placed in a Job Object, so it dies with the launcher if the launcher
  itself is killed.

## Building it alone

Same toolchain as the server (see `BUILDING.md`). From this directory, in a
Visual Studio + Qt 5.15 environment:

```
qmake launcher.pro CONFIG+=release
nmake release
```

The exe lands in `../wfweb-release/`. `build.bat` at the repository root does
this after the server build and runs `windeployqt` over both exes.

The command line builder and the log-line parser (`src/launchconfig.cpp`,
`src/logparser.cpp`) are plain Qt5Core code with no `Q_OBJECT`; they are
unit-tested by `tests/launcher_test.cpp` through `tests/test_cpp_units.py`.

## Icon

`res/wfweb.png` and `res/wfweb.ico` are rendered from wfweb's favicon,
`resources/web-shared/favicon.svg`, by `tools/make-launcher-icon.py`
(needs resvg-py and Pillow). They are committed; rerun the script only when
the favicon changes.
After regenerating, delete `launcher/release/wfweb-launcher_resource.res`
(or run `build.bat clean`) before rebuilding: the generated Makefile does not
list the `.ico` as a dependency, so the old compiled resource would be linked.

## Note on the LAN password

In LAN mode the password is passed as `--lan-pass`, which is visible in Task
Manager's command-line column to anyone who can see your processes, exactly
as if you had typed it into a console. The server stores it in plain text in
its settings file anyway, so this is no new exposure, but it is worth knowing.
