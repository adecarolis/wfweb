# SSTV codec shim

Upstream: https://github.com/unexcellent/sstv (Rust crate `sstv`)
Vendored commit: `9761c4c61a6353d7ae2173e1579e434e6c1b1ce3` (v0.3.1, 2026-10-03)
License: MIT (`vendor/sstv/LICENSE`). The notice also ships next to the compiled
module as `resources/web-standalone/wasm/sstv-LICENSE` and is in
`THIRD_PARTY_LICENSES`: update both when re-vendoring.

`vendor/sstv/` is the crate's `src/`, `build.rs`, `README.md` and `LICENSE`
at that commit, with `sstv-relock.patch` and `sstv-layout-api.patch`
applied and a trimmed `Cargo.toml`
(no examples, tests, dev-dependencies or the `image` / `wav` / `mp3`
extras). It is built with its default `std` feature (its `no_std` + `alloc`
build at this commit calls `f64::round` / `ceil`, which need std; the wasm
target has std).

## The patch: re-lock after a timing jump

`sstv-relock.patch` (`src/decode/assembler/cutter.rs`, `mod.rs`). Upstream
anchors each timing sequence on its sync pulse only when the pulse lands
within one pulse length (9 ms for Robot 36) of where nominal timing puts
it, and otherwise cuts at nominal timing for good. One audio frame dropped
or stalled on the way (a 20 ms silent frame from a TX underrun on the
bench, 2026-10-31; a lost packet or a sound-card underrun in the field)
moves every later sync pulse off by that amount, so none ever lands in the
window again and the rest of the picture decodes as garbage (wrong colours,
then wrong brightness). With the patch a sequence cut without its sync
pulse marks the timing lost; while lost, the cutter feeds every tone to
the crate's own `SyncLock` (the headerless acquisition: three sync pulses
one line apart, the Robot 36 line pair aligned by its separator tones) and
re-anchors the current sequence on what it finds. The two or three
sequences cut meanwhile stay garbled; everything after follows the signal
again (the bench capture went from mean error 105 to 7, four bad rows).
One extra sequence of frequency estimates is kept while lost so the lock
can score its control tones.

Worth offering upstream; until then the patch is carried here.

## The patch: a public sequence layout

`sstv-layout-api.patch` (`src/modes/mode.rs`) makes `sequence_duration`,
`sync_spacing`, `sync_pulse` and `has_starting_sync_pulse` public and adds
`control_tones()` (each fixed tone of the sequence with its offset). The
shim exports them as `sstv_mode_layout` / `sstv_mode_vis`, which the
worker's own image search (`resources/web-shared/sstv-worker.js`) uses to
name a mode from the spacing of its sync pulses and to align the Robot 36
line pair. No behaviour change in the crate.

`src/lib.rs` is wfweb's own code: a plain C ABI (no wasm-bindgen) that the
JS glue in `resources/web-standalone/wasm/sstv-codec.mjs` calls. The RX
path is Asyncified so the crate's pull-based decoder can wait for audio;
`tools/build-sstv-wasm.sh` explains why and does the build.

## Output

`resources/web-standalone/wasm/sstv.wasm`, committed. Both builds load it
(the server through a `web.qrc` alias, Standalone through
`tools/build-static.sh`'s `wasm/` copy step). Rebuild only when this shim
or the vendored crate changes.

## Toolchain (only to rebuild)

- Rust via rustup with the `wasm32-unknown-unknown` target
  (`rustup target add wasm32-unknown-unknown`); the distro `cargo` has no
  wasm32 standard library.
- `wasm-opt` (Binaryen, 110 or newer) on PATH or in `~/emsdk/upstream/bin`.

## Updating

1. Clone upstream at the new commit; `git apply sstv-relock.patch
   sstv-layout-api.patch` (drop the re-lock patch once upstream re-locks
   on its own; the layout one once it exposes the sequence); copy `src/`, `build.rs`,
   `README.md`, `LICENSE` over `vendor/sstv/`, keep our `Cargo.toml` there
   unless upstream's dependencies changed; update the commit above.
2. `tools/build-sstv-wasm.sh` (refreshes `Cargo.lock` and `sstv.wasm`).
3. `python3 -m pytest tests/test_sstv.py` (round trips through the real
   worker under node, including a picture with a 20 ms frame inserted and
   one dropped).
4. Commit `vendor/`, `Cargo.lock`, this file and `sstv.wasm` together.
