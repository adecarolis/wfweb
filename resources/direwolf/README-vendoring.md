# Dire Wolf — vendored subset

Upstream: https://github.com/wb2osz/direwolf
Commit:   a231971a652bfb574a4bae9a5d875fbce53d2267
Imported: 2026-04-25
License:  GPLv2-or-later (see `LICENSE`)

Only the modem, HDLC framing, and AX.25 packet core are vendored here.
Audio I/O, config parsing, PTT, IGate, digipeater, KISS servers, APRS-IS,
FX.25 / IL2P FEC, and the `main()` drivers are intentionally omitted —
wfweb provides its own audio bus and PTT path, and the host app only
needs the modem + framing layer.

## Files taken from upstream `src/` (verbatim unless patched below)

**C sources**
- ax25_pad.c, ax25_pad2.c
- demod.c, demod_afsk.c, demod_9600.c
- multi_modem.c
- hdlc_rec.c, hdlc_rec2.c, hdlc_send.c
- gen_tone.c
- fcs_calc.c
- dsp.c
- dtime_now.c
- rrbb.c

**Headers**
- ax25_pad.h, ax25_pad2.h
- demod.h, demod_afsk.h, demod_9600.h
- multi_modem.h
- hdlc_rec.h, hdlc_rec2.h, hdlc_send.h
- gen_tone.h
- fcs_calc.h
- dsp.h
- dtime_now.h
- rrbb.h
- fsk_demod_state.h, fsk_filters.h
- direwolf.h, textcolor.h
- audio.h, gpio_common.h, version.h, tune.h
- il2p.h, fx25.h (type declarations only — `.c` files not vendored)

## Local patches

1. **`direwolf.h`** — guard `MAX_RADIO_CHANS`, `MAX_SUBCHANS`,
   `MAX_SLICERS`, `MAX_ADEVS` with `#ifndef` and drop defaults to
   `2 / 1 / 3 / 1` respectively. Keeps per-instance static state small
   without breaking upstream callers who override.

2. **`demod_afsk.c`** — replace `exit(1)` in the unknown-profile init
   path with a `dw_printf` log and `return` so the host can recover.

3. **`direwolf.h`** (MSVC compat) — replace the `#error` guards on
   `_WIN32_WINNT` / `WINVER` with `#ifndef`. Qt and the MSVC runtime
   pull `<windows.h>` in before Direwolf, so the upstream check trips
   on every translation unit. The `#ifndef` form respects whatever
   value the host has already set and only falls back to `0x0501` when
   nothing is defined.

4. **`direwolf.h`** (MSVC compat) — change the pthread-include guard
   from `#if __WIN32__` to `#if __WIN32__ || defined(_WIN32)`. MSVC
   doesn't predefine `__WIN32__` (only MinGW does), so without this
   the POSIX branch would try to `#include <pthread.h>`. wfweb.pro
   also forces `__WIN32__=1` on `win32-msvc` so the rest of the
   `#if __WIN32__` branches in `dlq.c`, `dtime_now.c`, `ax25_pad.c`,
   etc. take their Windows code paths consistently — no pthread
   emulation needed.

5. **`direwolf.h`** (MSVC compat) — add a `__restrict__` → `__restrict`
   alias for MSVC. The `strlcpy_debug` / `strlcat_debug` prototypes
   use the GCC-only `__restrict__` spelling; MSVC understands
   `__restrict` (and the C99 `restrict`) but not the underscored form.
   Same block also `#define`s `__attribute__(x)` to nothing on MSVC,
   neutralising the GCC-only `__attribute__((format(...)))` hint on
   `dw_printf` (textcolor.h) and the `__attribute__((aligned(16)))`
   markers on float arrays in fsk_demod_state.h. The vendored modem
   subset uses no SSE intrinsics that would actually require 16-byte
   alignment, so dropping the hint is correctness-neutral.

6. **`ax25_pad.h` / `ax25_pad.c`** (MSVC compat) — change the
   `src_file` parameter of `ax25_from_text_debug`, `ax25_from_frame_debug`,
   `ax25_dup_debug`, and `ax25_delete_debug` from `char *` to
   `const char *`. The macros that wrap them pass `__FILE__`, which
   under MSVC's `Zc:strictStrings` is `const char[N]` and won't
   implicitly bind to `char *`. GCC tolerates this; MSVC errors out.

## MSVC POSIX-header shims (`msvc-shim/`)

`msvc-shim/unistd.h` and `msvc-shim/regex.h` are empty stubs added
to `INCLUDEPATH` ahead of the system headers on `win32-msvc` builds.
Several vendored sources include `<unistd.h>` (and `ax25_pad.c`
includes `<regex.h>`) unconditionally even though no symbols from
those headers are actually called in the modem subset we vendor —
the includes are leftovers from upstream code paths we don't compile.

## Companion file (not from upstream)

- `wfweb_direwolf_stubs.c` — provides the symbols Dire Wolf expects to
  exist at link time but that we do not vendor:
  - `dw_printf`, `text_color_set`, `text_color_init`, `text_color_term`
  - `audio_put`, `audio_get`, `audio_flush`, `audio_wait`, `audio_open`,
    `audio_close`
  - `dlq_rec_frame` (RX frame hook, routes to `DireWolfProcessor`)
  - `ptt_set`, `ptt_init`, `ptt_term`
  - `fx25_rec_bit`, `il2p_rec_bit`, `il2p_init`, `fx25_init`

## Updating from upstream

1. `git fetch` in a temporary clone of upstream.
2. Copy the file list above.
3. Re-apply the two patches.
4. Rebuild with `CONFIG += packet` and run the verification matrix in
   `CLAUDE.md`.
