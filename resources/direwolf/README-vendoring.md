# Dire Wolf — vendored subset

Upstream: https://github.com/wb2osz/direwolf
Commit:   a231971a652bfb574a4bae9a5d875fbce53d2267
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
