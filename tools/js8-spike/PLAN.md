# JS8Call port plan

A working JS8 (TX + RX, all speed modes, structured-frame messaging) panel
in wfweb's web SPA, served by both the Qt5 server build and the static
standalone build, end-to-end-tested against the official JS8Call client
through the existing testrig bench.

Source of truth for the codec: **JS8Call-improved**
(<https://github.com/JS8Call-improved/JS8Call-improved>) — pure C++, GPL-3,
no Fortran. Same license family as wfweb.

---

## Spike findings (everything that follows depends on these)

| Spike | Outcome | Artifact in this dir | Implication |
|---|---|---|---|
| **Encoder** standalone compile | `JS8::encode()` builds verbatim under emcc → 14 KB .wasm; native + node both produce correct tones (3 Costas arrays at offsets 0/36/72, all 79 outputs in [0,7]) | `js8_encode.cpp`, `test_encode_raw.mjs` | TX side is essentially solved at the codec level. Only one boost dep (`augmented_crc`), replaceable in ~20 lines or via emcc's `-sUSE_BOOST_HEADERS=1`. No Qt, no FFTW, no Eigen. |
| **Qt-string shim** for the decoder text-extraction layer | 270 LOC of `<string>`-backed shim; `DecodedText.cpp` (344 lines, real upstream source) compiles + runs **verbatim** native + emcc; reproduces every QString operation the file uses including `arg(int, fieldWidth, base, fillChar)` and QStringBuilder `%` concat | `varicode-shim/` | Path A (hand shim) is feasible. We do **not** need Qt 6 WASM SDK. Real Varicode unpacks will likely add ~5 more methods to the shim (`toUpper`, `toLower`, `replace`, `startsWith`, `indexOf`); call it ~400–500 LOC final. |
| **FFTW under emcc** | `libfftw3f.a` builds clean (`emconfigure ./configure --enable-float --host=i686-linux-gnu …` + `emmake make`); test program produces correct DFT (pure tone bin 5, magnitude N) | `fftw/` | Standard route works. Mandatory flag: `--enable-float` (JS8.cpp uses `fftwf_*` not `fftw_*`). FFTW config.sub doesn't know `wasm32-unknown-emscripten` — use `--host=i686-linux-gnu` and let emconfigure inject the cross compiler. |

**Bottom line:** every speculative dependency has been proven. No more
exploratory work needed before Phase 0 starts.

---

## Architecture — what lives where

```
resources/js8-src/                    ← NEW. Vendored upstream subset.
  README-vendoring.md                 ← what was kept, what was cut, upstream ref
  JS8_Include/
    commons.h                         ← unchanged
  JS8_Mode/                           ← bulk of the codec
    JS8.{cpp,h}                       ← stripped: keep encode(), decode(),
                                        DecodeMode<>, FFTW glue. DROP the
                                        Decoder QObject + Worker class.
    JS8Submode.{cpp,h}                ← per-mode params (Normal/Fast/Turbo/Slow/Ultra)
    DecodedText.{cpp,h}               ← unchanged (uses qt-shim transparently)
    FrequencyTracker.{cpp,h}          ← unchanged (zero-Qt already)
    ldpc_feedback.h                   ← unchanged
    soft_combiner.h                   ← unchanged
    whitening_processor.h             ← unchanged
  JS8_Main/
    Varicode.{cpp,h}                  ← unchanged (uses qt-shim transparently)
    DriftingDateTime.h                ← unchanged
  qt-shim/                            ← NEW. ~400 LOC of std-backed Qt headers.
    QtGlobal QChar QString QStringList QStringBuilder QStringLiteral
    QByteArray QMap QVector QPair QList QDebug QLoggingCategory
    QtAlgorithms QRegularExpression  ← only the ones JS8 codec subset touches
  vendor/                             ← upstream's vendor tree, subset
    CRCpp/CRC.h                       ← header-only, used by Varicode
    Eigen/                            ← already in wfweb; symlink to wfweb's copy
  api/
    js8_wasm_api.{cpp,h}              ← OUR thin C entry points
                                        js8_encode, js8_decode, js8_set_mode,
                                        js8_alloc_decode_state, js8_free_*

tools/build-js8-wasm.sh               ← NEW. Mirrors build-direwolf-wasm.sh.
tools/build-js8-fftw.sh               ← NEW. One-time. Caches libfftw3f.a.

resources/web-standalone/wasm/
  js8.mjs                             ← committed build output
  js8.wasm                            ← committed build output

resources/web-shared/                 ← integration into the SPA
  js8.js                              ← Web Worker harness mirroring ft8ts.mjs
  packet.js, models/, …               ← unchanged

resources/web/index.html              ← Server SPA (DIGI panel gains JS8 mode)
resources/web-standalone/index.html   ← Standalone SPA (same)
```

### Why a single WASM module (not separate encode + decode)

The encoder is pure but ~15 KB; the decoder is heavy (FFTW + LDPC + sync,
~1–2 MB). Bundling them keeps the build chain simple and lets the encoder
share Eigen and the Varicode constants with the decoder. The `js8.wasm`
ends up ~1.5–2.5 MB; loaded by Web Worker on demand, only in the JS8 panel.

### Tier 1 vs Tier 2

- **Tier 1** — structured frames (CQ, HB, compound, directed, data-with-known-format).
  Ships with everything in this plan. Sufficient for ~90% of real JS8 traffic.
- **Tier 2** — free-text Huffman decompression (the JSC dictionary). 14 MB of
  generated source code → +1.5–3 MB to the WASM. **Out of scope for this
  plan.** Add later under a `?freetext=1` URL param or lazy-load if needed.

---

## Phase 0 — Foundation (3–5 days)

### Day 1 — Vendoring

- Branch off `dev`: `js8/phase-0-foundation`.
- Add `resources/js8-src/` mirroring upstream layout (see Architecture).
- Copy `JS8_Mode/{JS8.cpp,JS8.h,JS8Submode.cpp,JS8Submode.h,DecodedText.cpp,
  DecodedText.h,FrequencyTracker.cpp,FrequencyTracker.h,ldpc_feedback.h,
  soft_combiner.h,whitening_processor.h}`.
- Copy `JS8_Main/{Varicode.cpp,Varicode.h,DriftingDateTime.h}`.
- Copy `JS8_Include/commons.h`.
- Copy `vendor/CRCpp/CRC.h`.
- Symlink `vendor/Eigen` → wfweb's existing Eigen.
- **Edit `JS8.h`**: delete the `Decoder : public QObject` class and `Worker`
  forward decl. Keep namespace JS8 with `Costas`, `encode()` decl,
  `Event::*` structs (Decoded, DecodeStarted, …). The QObject machinery is
  application glue we replace; the codec types are needed.
- **Edit `JS8.cpp`**: remove the `Decoder::*` and `Worker::*` method
  implementations + their includes (`QSemaphore`, `QThread`). Keep the
  `encode()` free function and the `decode()` free functions inside `JS8::`.
  Remove `Q_DECLARE_LOGGING_CATEGORY` line and the `decoder_js8` references
  (replace with no-op macro from qt-shim).
- Write `resources/js8-src/README-vendoring.md` with: upstream commit SHA,
  files kept, files dropped, files modified (with reason for each).

**Gate:** `git ls-files resources/js8-src/` shows the expected tree;
`README-vendoring.md` exists; nothing builds yet.

### Day 2 — Qt shim

- Copy `tools/js8-spike/varicode-shim/shim/*` into
  `resources/js8-src/qt-shim/`.
- Add the headers Varicode.cpp transitively requires beyond what
  DecodedText.cpp needed:
  - `QByteArray` (std::string-backed; CRCpp + boost::format use it)
  - `QMap` (std::map wrapper; Varicode huffman tables)
  - `QVector` / `QList` (std::vector wrapper)
  - `QPair` (std::pair wrapper)
  - `QSet` (std::set wrapper)
  - `QChar::Direction` etc. enums Varicode needs
  - `QRegularExpression` + `QRegularExpressionMatch` (std::regex wrapper —
    smallest surface; only `match()` and `captured()` likely needed)
  - `QDebug` (no-op stream that swallows everything)
  - `QLoggingCategory` + `Q_DECLARE_LOGGING_CATEGORY` macro (no-op)
  - `QtAlgorithms` (forwards to `<algorithm>`)
- Compile a smoke target that includes every vendored .h once.
  **Gate:** `g++ -std=c++20 -I qt-shim -I . -fsyntax-only smoketest.cpp`
  passes with no warnings.

### Day 3 — FFTW one-time build

- Write `tools/build-js8-fftw.sh`:
  ```sh
  #!/bin/sh
  set -e
  REPO=$(cd "$(dirname "$0")/.." && pwd)
  CACHE="$REPO/resources/js8-src/.fftw-cache"
  mkdir -p "$CACHE" && cd "$CACHE"
  if [ -f libfftw3f.a ]; then
    echo "FFTW already built — delete .fftw-cache to rebuild"; exit 0
  fi
  [ -d fftw-3.3.10 ] || {
    curl -sL https://www.fftw.org/fftw-3.3.10.tar.gz | tar -xz
  }
  cd fftw-3.3.10
  emconfigure ./configure \
      --enable-float --disable-shared --disable-fortran \
      --disable-doc --disable-threads --enable-static \
      --host=i686-linux-gnu          # config.sub doesn't know wasm-emscripten
  emmake make -j$(nproc)
  cp .libs/libfftw3f.a "$CACHE/"
  cp api/fftw3.h "$CACHE/"
  ```
- Add `.fftw-cache/` to `.gitignore`. The `.a` is host-architecture-independent
  inside emcc but takes ~2 min to build — caching avoids that on every CI run.
- Run it once locally; verify `libfftw3f.a` and `fftw3.h` land in the cache.

**Gate:** `tools/build-js8-fftw.sh` is idempotent; second run prints "already
built" and exits 0. `libfftw3f.a` is in the cache.

### Day 4 — Build script + thin C API

- Write `resources/js8-src/api/js8_wasm_api.h`:
  ```c
  #pragma once
  #ifdef __cplusplus
  extern "C" {
  #endif

  // Encode: 12-char message + frame type → 79 tones (0..7)
  int js8_encode(int frame_type, const char* msg, int* tones_out);

  // Decode: opaque state, fed audio chunks, yields decoded messages as JSON.
  // Caller polls js8_decode_pop() to drain.
  typedef struct js8_decoder js8_decoder;
  js8_decoder* js8_decoder_new(int submode);            // 0=Normal etc.
  void          js8_decoder_free(js8_decoder*);
  int           js8_decoder_push(js8_decoder*, const float* samples, int n);
  // Returns NULL when nothing more to drain. Caller must free the returned
  // string with js8_free_string(). String is JSON: [{snr,dt,freq,text,type},...]
  char*         js8_decoder_pop(js8_decoder*);
  void          js8_free_string(char*);

  #ifdef __cplusplus
  }
  #endif
  ```
- Write `js8_wasm_api.cpp` that wraps `JS8::encode()` and a stripped-down
  decode loop that calls JS8.cpp's free decode functions directly (no
  QObject Decoder), accumulates results, and exposes them as JSON via
  `nlohmann::json` (header-only, vendor it under `vendor/json.hpp`) or
  hand-rolled string concatenation (simpler, no extra dep).
- Write `tools/build-js8-wasm.sh`:
  ```sh
  #!/bin/sh
  set -e
  REPO=$(cd "$(dirname "$0")/.." && pwd)
  SRC="$REPO/resources/js8-src"
  OUT="$REPO/resources/web-standalone/wasm"
  CACHE="$SRC/.fftw-cache"

  [ -f "$CACHE/libfftw3f.a" ] || "$REPO/tools/build-js8-fftw.sh"

  mkdir -p "$OUT"
  emcc -std=c++20 -O3 -fno-rtti \
      -DJS8_WASM=1 \
      -I "$SRC" -I "$SRC/qt-shim" -I "$CACHE" \
      -I "$SRC/vendor" \
      -sUSE_BOOST_HEADERS=1 \
      "$SRC/JS8_Mode/JS8.cpp" \
      "$SRC/JS8_Mode/JS8Submode.cpp" \
      "$SRC/JS8_Mode/DecodedText.cpp" \
      "$SRC/JS8_Mode/FrequencyTracker.cpp" \
      "$SRC/JS8_Main/Varicode.cpp" \
      "$SRC/api/js8_wasm_api.cpp" \
      "$CACHE/libfftw3f.a" \
      -lm \
      -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 \
      -s EXPORT_NAME=createJS8 \
      -s EXPORTED_FUNCTIONS='[
          "_js8_encode",
          "_js8_decoder_new","_js8_decoder_free",
          "_js8_decoder_push","_js8_decoder_pop",
          "_js8_free_string","_malloc","_free"]' \
      -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAPF32"]' \
      -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=33554432 \
      -s ENVIRONMENT=web,worker \
      -o "$OUT/js8.mjs"
  ```
- First end-to-end build. Expect 5–15 min the first time (FFTW + many codelet
  TUs); subsequent rebuilds are fast because everything but the codec is
  cached.

**Gate:** `tools/build-js8-wasm.sh` produces `resources/web-standalone/wasm/js8.{mjs,wasm}`. `js8_encode` exported and callable from a Node test harness, returns the same 79-tone array as the spike. `js8_decoder_new/free` link cleanly even if `_push` returns 0 results.

### Day 5 — Web Worker harness + Server build wiring

- Write `resources/web-shared/js8.js` modeled after `ft8ts.mjs` integration:
  - Initialize the Module via `import("./wasm/js8.mjs")`
  - Allocate a 12 kHz buffer; downsample 48→12 using the same phase-accumulator
    pattern `processDigiAudioChunk()` already uses for FT8
  - Run decode at slot boundaries (slot length is per-submode: see
    `JS8Submode::tx_seconds` — 30/15.6/10/6 s)
  - Drain `js8_decoder_pop()` after each push, parse JSON, return array of
    decode results to the main thread
- Add `resources/web.qrc` aliases for `js8.mjs` and `js8.wasm` so the Server
  build serves them at `/web/wasm/js8.{mjs,wasm}`.
- Add the wasm files to `tools/build-static.sh`'s file list so they're copied
  into the static `dist/`.

**Gate:** Both builds load `js8.js` without errors. `js8_encode()` round-trips
matches Phase 0 spike. Decoder accepts samples without crashing (still returns
empty results — real decode comes in Phase 1).

**End-of-Phase-0 commit:** `feat(wasm): JS8 codec foundation (encode works, decode stubbed)` on `js8/phase-0-foundation`.

---

## Phase 1 — Decoder MVP, Normal mode (2–3 weeks)

The bulk of the protocol-level work. Phase 0's stub `js8_decoder_pop()`
becomes a real decoder.

### Week 1 — get JS8.cpp's decode path running

The key calls inside `JS8.cpp`:
- `syncjs8()` — frequency / time sync candidate detection
- `decode8jsa()`, `decode8jsb()`, … — per-submode decode (BP LDPC + soft combiner)
- `unpack_message()` — bits → text (calls Varicode methods)

The QObject `Decoder::Worker::process()` orchestrates these. We replace
`process()` with a simple C-callable function that takes a recent audio
buffer + a target submode and returns decode events.

Write tests that take a known-good audio capture (made in Phase 1.1 below)
and assert exact decoded text + SNR ranges.

### Week 1.1 — gold corpus

- Use the testrig: drive JS8Call as the transmitter, run virtualrig with
  `--external 1` and route audio out to a WAV file via PulseAudio's
  `module-recorder`.
- Capture 20 known transmissions: 5 each of CQ / directed / heartbeat / compound.
- Save `resources/js8-src/test/corpus/{normal,...}/{type}_{n}.wav` plus a
  `.json` per file with the expected decode (ground truth from JS8Call's
  ALL.TXT).
- Test runner: `tools/test-js8-decode.sh`. Plays each WAV through the decoder,
  compares against the JSON.

### Week 2 — fix the discrepancies

- Iterate on whatever parts of the decoder fail. Most likely culprits:
  - Sync detection sensitivity (runs over the test bench at controlled SNR)
  - LDPC BP iteration count
  - Soft combiner threshold
  - Frequency tracking convergence
- Each fix shows up as `git diff resources/js8-src/JS8_Mode/JS8.cpp` — the
  point of vendoring rather than upstreaming is that we own the diff.

### Week 3 — testrig roundtrip

- Bring up `./scripts/testrig.sh up 1 1`. Slot 0 = wfweb (browser), slot 1 =
  external (JS8Call).
- JS8Call transmits → wfweb's panel decodes → text matches what JS8Call's
  log shows for the same transmission.
- Hit-rate target: **100% on clean bus, ≥80% at noise=200**.

**Gate:** All 20 corpus messages decode correctly + the testrig roundtrip
passes.

**End-of-Phase-1 commit:** `feat(js8): real RX decode for JS8Normal`.

---

## Phase 2 — Encoder integration (2–3 days)

The codec already works (proven in Phase 0). What's left is wiring TX into
wfweb's existing audio path:

1. JS frontend: synthesize 8-FSK audio from the 79-tone array at the correct
   submode-specific symbol rate (Normal: 1920 samples/symbol @ 12 kHz =
   6.4 ms/symbol → 79 × 6.4 ms ≈ 0.5 s of TX; pre-roll + transmission window
   fits the 15 s slot).
2. Reuse the same upsample-12k→48k + WebSocket frame plumbing FT8 uses.
3. Integrate with the slot scheduler (TX fires at slot start + small delay).
4. PTT handling: `setPTT(true)` at TX start; release on completion (300 ms
   tail like RADE EOO).

Test against testrig the other direction: wfweb transmits → JS8Call decodes
with text match.

**Gate:** JS8Call decodes 100% of wfweb transmissions on clean bus.

**End-of-Phase-2 commit:** `feat(js8): TX support for JS8Normal`.

---

## Phase 3 — Other speed modes (3–5 days)

Slow / Fast / Turbo / Ultra differ from Normal only in:
- Symbol rate (`SYMBOL_SAMPLES`)
- Slot duration (`TX_SECONDS`)
- Sync offsets (`START_DELAY_MS`)

All parameterized by `JS8Submode`. The C++ already handles the other modes
(`decode8jsb`, `decode8jsc`, etc.). Work for each mode:

1. Add submode case in `js8_decoder_new`.
2. Add slot-timing case in the JS Worker harness (`getDigiSlotInfo()` already
   knows about FT8/FT4; generalize).
3. Add encode submode case in TX path.
4. Add corpus test files for each mode.
5. Testrig roundtrip for each mode.

**Gate:** All four submodes pass corpus tests + testrig roundtrip.

**End-of-Phase-3 commit:** `feat(js8): JS8Slow, Fast, Turbo, Ultra modes`.

---

## Phase 4 — UI panel (2–3 weeks)

Lives in `resources/web-shared/` (shared between both builds; pattern from
`ft8ts.mjs` integration). Builds on the existing DIGI panel structure.

### Week 1 — minimum viable panel

- Mode selector: USB / FT8 / FT4 / **JS8 (Normal/Slow/Fast/Turbo)**
- Slot countdown + current submode display
- Decode list: scrolling pane with `[time SNR freq] FROM>TO message`
- Waterfall already exists from FT8 panel — reuse, with JS8 sync markers
- Free-text RX window: receive-only chat-style display
- Compose box + TX button: submit a 12-char message into the TX queue

### Week 2 — JS8-specific UX

- **Callsigns of interest (COI)**: persistent list in localStorage; decode
  list highlights / filters
- **Directed-message inbox**: messages addressed to the user's callsign
- **Heartbeat list**: stations heard, last-seen timestamps
- **Frame-type badges**: CQ / HB / DIR / DATA color-coded in the decode list
- **CQ button + auto-CQ checkbox**

### Week 3 — polish + persistence

- Persist: callsign, grid, COI list, recent decode log, preferred submode
- Mobile-friendly layout for the standalone build
- Match the existing wfweb visual language (gain-bar styling family)

**Gate:** A non-trivial JS8 QSO can be conducted from the wfweb UI alone
against an opposing JS8Call client on the testrig.

**End-of-Phase-4 commit:** `feat(js8): UI panel + messaging UX`.

---

## Phase 5 — Polish (1 week)

- Group calls (`@GROUP message`) — Varicode already handles them, just UI
- Relay (`>` operator) — Varicode handles; needs UI affordance
- ACK indication
- ALL.TXT-equivalent CSV log download (matching what JS8Call writes)
- CHANGELOG entry
- Version bump
- Docs in `BUILDING.md` for the new wasm dep

**Gate:** Release candidate. Run `./scripts/testrig.sh up 1 1` for a full
day; review the resulting ALL.TXT against JS8Call's; no decode discrepancies.

**End-of-Phase-5 commit:** `feat(js8): groups, relay, ACK, log export` then `release: v0.8.0`.

---

## Test strategy

Three layers, in increasing complexity:

1. **Codec unit tests** (`resources/js8-src/test/`):
   - encode round-trip: encode → decode (against the vendored decoder) →
     output == input
   - corpus tests: known WAV → expected decoded text
2. **WASM smoke** (`tools/test-js8-wasm.sh`):
   - Loads `js8.{mjs,wasm}` in Node, runs encode + decode on synthetic
     samples. Asserts no crashes, no leaks (compare malloc/free counts).
3. **Testrig integration** (`./scripts/testrig.sh up 1 1`):
   - Roundtrip with the official JS8Call client through the shared bus.
   - Both directions, all four submodes, at three noise levels.

CI runs (1) and (2). (3) is local-only — it needs JS8Call installed and
PulseAudio configured.

---

## Risks and mitigations (post-spike)

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Real Varicode unpacks need more QString methods than the spike covered | High | Low | Extend the shim incrementally as compile errors surface. Each method is ~5–10 LOC. Budget half a day in Phase 0 day 2. |
| FFTW codelet linkage explodes the binary | Medium | Medium | Build with `-O3` (dead-code-eliminates unused codelets); if still too big, vendor `pffft` instead (single-precision, 1500 LOC, FFTW-API-compatible mode exists). |
| JS8.cpp's QObject machinery has subtle entanglement we missed | Medium | Medium | If `decode()` calls back into Decoder via `m_emitter`, replace the emitter callback with a C function pointer. Should be a search-replace, not a rewrite. |
| Boost::multi_index doesn't compile under emcc clean | Low | Medium | `-sUSE_BOOST_HEADERS=1` + Emscripten 3.1.x has been tested with multi_index in other projects. If it fails, the use is in a station-tracking cache that's replaceable with `std::map`. |
| Decode performance too slow in WASM | Medium | Low | JS8 frames are 6–30 s, decode budget is generous. If we can't decode in real time on a phone, the panel falls back to "after-the-fact" decode (still useful). |
| Free-text decompression (Tier 2) requested before plan is done | Medium | Low | Out of scope. Document deferral; offer a "free-text gibberish placeholder" in the decode list for FrameDataCompressed. |
| ALL.TXT format drift from JS8Call upstream | Low | Low | Don't promise format-byte parity. Match field set, not byte-for-byte. |

---

## Open decisions (with my recommendation)

1. **Where the WASM lives** — `resources/web-standalone/wasm/` (committed)
   matching RADE/Direwolf, or fetched at deploy time?
   **Recommend committed.** Same as siblings, no fetch latency, easy `git
   blame` on the binary, no deploy-time toolchain.

2. **JSON parsing on the JS side** — vendor `nlohmann/json` for the C++ side
   or hand-roll string concatenation?
   **Recommend hand-roll.** Output is a JSON array of fixed-shape objects;
   `printf("[%g,%g,…]")` is 30 lines; nlohmann adds 100 KB of templates.

3. **FFTW vs pffft** — confirmed FFTW works; pffft would be a smaller binary.
   **Recommend FFTW.** Spike confirmed it; substituting later costs
   ~2 days if size becomes a problem. Don't optimize prematurely.

4. **Tier 2 (free-text) inclusion** — ship-ready or deferred to v0.9?
   **Recommend deferred.** +14 MB source / +1.5–3 MB WASM is a real cost;
   ship Tier 1 first, then gauge demand.

5. **Reporter integration** — JS8Call has no central reporter (PSK/FreeDV
   models don't apply); skip?
   **Recommend skip.** JS8 is opportunistic-relay-based by design.

6. **Server build vs Standalone scope** — both, or Standalone first?
   **Recommend both, simultaneously.** The shared file pattern means the
   marginal cost of supporting both is one `web.qrc` alias entry.

---

## Out of scope

- Tier 2 (free-text Huffman decompression) — separate phase later
- Reporter / spotting service
- ALL.TXT byte-for-byte format compatibility
- Multi-rig JS8 monitoring (we only support one slot at a time)
- Cross-mode wfweb → JS8Call interop other than audio-bus + CAT testrig
- Windows / macOS native rebuilds of the wasm step (Linux only for the build
  toolchain; output runs everywhere)

---

## Total budget

| Phase | Days |
|---|---|
| 0 — Foundation | 5 |
| 1 — Decoder MVP (Normal) | 14 |
| 2 — Encoder integration | 2 |
| 3 — Other submodes | 4 |
| 4 — UI | 14 |
| 5 — Polish | 5 |
| **Total** | **44 days ≈ 6–8 weeks calendar** |

Spike artifacts that informed every estimate above live in this directory:

```
tools/js8-spike/
├── PLAN.md                          ← this file
├── README.md                        ← encoder spike summary
├── js8_encode.cpp                   ← encoder spike code
├── test_encode_raw.mjs              ← encoder spike runner
├── varicode-shim/                   ← Qt shim spike
│   ├── README.md
│   ├── shim/                        ← the 270 LOC starting point
│   ├── vendor/                      ← DecodedText + JS8 stub + Varicode stub
│   └── test_decoded.cpp
└── fftw/                            ← FFTW build spike
    ├── README.md
    └── fft_test.c
```

**Delete this directory** when Phase 5 ships — it's scaffolding, not docs.
