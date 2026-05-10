# Phase 0 status

Branch: `js8/phase-0-foundation`. Day 5 still pending.

## What works

| Day | Deliverable | State |
|---|---|---|
| 1 | Vendor JS8_Mode + JS8_Main + JS8_Include + vendor/{CRCpp,Eigen} | DONE |
| 1 | `JS8.h` / `JS8.cpp` stripped of QObject Decoder/Worker | DONE |
| 1 | `README-vendoring.md` with upstream SHA + diff catalog | DONE |
| 2 | Qt shim: 23 headers covering QString/QStringList/QChar/QByteArray/QMap/QVector/QList/QSet/QPair/QBitArray/QStringView/QStringBuilder/QStringLiteral/QRegularExpression(+Match,Iterator)/QRegExp/QDebug/QLoggingCategory/QObject/QtAlgorithms/QtMath/QMutex/QPointer/QThread/QtGlobal | DONE |
| 2 | `qt-shim/_smoketest.cpp` compiles (every shim + every codec header includes clean) | PASS |
| 3 | `tools/build-js8-fftw.sh` builds + caches `libfftw3f.a` (~890 KB) under `.fftw-cache/`. Idempotent. | PASS |
| 4 | `api/js8_wasm_api.{h,cpp}` — C-callable surface (encode + decoder lifecycle) | DONE |
| 4 | `JS8.cpp` factory functions `js8_make_decoder` / `js8_run_decoder` linking `DecoderImpl` across TUs | DONE |
| 4 | `tools/build-js8-wasm.sh` orchestration (FFTW + cache bootstrap + emcc + USE_BOOST_HEADERS) | DONE |
| 4 | First `js8.{mjs,wasm}` artifact emitted | **BLOCKED** — see below |

## Phase 0 day 4 blockers (real, not "I gave up")

### B1. Debian's emcc 3.1.6 ships libc++ 14, which has known bugs

- **`std::visit` with overloaded function-object alternatives** — return-type
  deduction fails on the dispatch template. **Worked around in source**:
  patched `JS8.cpp:DecoderImpl::operator()` to use manual `std::get_if`
  dispatch instead of `std::visit`. Vendor diff documented in
  `JS8.cpp` next to the change.

- **Boost-headers port pinned to Boost 1.75** (in emcc 3.1.6's port
  registry). `<boost/math/ccmath/round.hpp>` was added in Boost 1.79.
  **Worked around in source**: replaced `using boost::math::ccmath::round;`
  with a constexpr `round` lambda. Vendor diff in `JS8.cpp`.

These two source patches mean we can never fully roll forward against
fresh upstream without re-applying them — until the host emcc updates.

### B2. Compile is still failing on `Varicode.cpp` against the shim

Each iteration has uncovered another surface bit Varicode uses that the
shim doesn't yet handle. Latest in-progress error: a `QString(const
QChar*, int)` constructor overload (raw QChar array → string). After
that, more iterations are likely needed before the codec subset compiles
end-to-end.

## What this means for Phase 0's gate

Per `tools/js8-spike/PLAN.md`:
> **Gate:** `tools/build-js8-wasm.sh` produces `resources/web-standalone/wasm/js8.{mjs,wasm}`. `js8_encode` exported and callable from a Node test harness, returns the same 79-tone array as the spike. `js8_decoder_new/free` link cleanly even if `_push` returns 0 results.

Day 4 gate is **not yet met**. We have the right structure (build
script, C API, factory functions, all toolchain bits) but the shim still
needs ~5–10 more iterations of "compile, find missing method, add it,
rebuild" before Varicode.cpp links cleanly.

The PLAN.md estimate ("3–5 days") for Phase 0 was based on the spike
having already proven the path. The spike's target was DecodedText.cpp
(344 lines, 5 Qt types). Varicode.cpp is 2370 lines + JSC stubs + the
JS8 codec's own surface; the actual shim it needs is bigger than the
spike's measurement. **Realistic Phase 0 day 4 budget: another 1–2 days
of iteration**, then the real Phase 1 starts.

## Next session pickup list

1. Iterate `tools/build-js8-wasm.sh` against compile errors one by one,
   adding missing shim methods or vendor patches.
2. Common shim additions likely needed:
   - `QString(const QChar*, int)` constructor
   - `QString::number(double)`, `QString::number(int, int base)`
   - More `QString::arg` overloads (with `double`, with multiple args)
   - `QStringList::contains`, `QStringList::indexOf`
   - `QMap::insert(K, V)` returning iterator, `find` returning iterator
   - `QSet::insert` returning iterator
3. Once Varicode.cpp links: link the rest, produce `js8.mjs/wasm`, write
   a Node test harness mirroring the encoder spike.
4. Day 5 (Web Worker harness in `resources/web-shared/js8.js`) is
   unaffected — can be drafted in parallel against the C API surface
   already declared in `js8_wasm_api.h`.

## Updated Phase 0 timing estimate

| | PLAN.md | Actual so far |
|---|---|---|
| Day 1 (vendor) | 1 day | done in <1 day |
| Day 2 (shim) | 1 day | spike ≈ 1 hour, real shim maybe 2–3 days when fully done |
| Day 3 (FFTW) | 1 day | done in <1 day |
| Day 4 (build) | 1–2 days | in progress; +1–2 days remaining |
| Day 5 (worker) | 1 day | not started |
| **Total** | 3–5 days | **6–8 days** likely |

The Phase 1–5 estimates should still hold once Phase 0 lands.
