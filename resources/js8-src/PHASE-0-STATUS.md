# Phase 0 status — COMPLETE

Branch: `js8/phase-0-foundation`. All five days done.

## Final gates

| Day | Deliverable | State |
|---|---|---|
| 1 | Vendor JS8_Mode + JS8_Main + JS8_Include + JSC stub + vendor/{CRCpp,Eigen} | PASS |
| 1 | `JS8.h` / `JS8.cpp` stripped of QObject Decoder/Worker; DecoderImpl hoisted out | PASS |
| 1 | `README-vendoring.md` documents upstream commit + every diff | PASS |
| 2 | Qt shim — 24 headers covering all the types + macros the codec uses | PASS |
| 2 | `qt-shim/_smoketest.cpp` compiles clean against shim + every codec header | PASS |
| 3 | `tools/build-js8-fftw.sh` builds + caches `libfftw3f.a` (~890 KB), idempotent | PASS |
| 4 | `api/js8_wasm_api.{h,cpp}` — C-callable surface (encode + decoder lifecycle) | PASS |
| 4 | `tools/build-js8-wasm.sh` produces `js8.{mjs,wasm}` (37 KB / 1.0 MB) | PASS |
| 4 | `tools/test-js8-wasm.mjs` end-to-end smoke: encode round-trip + decoder lifecycle | PASS |
| 5 | `resources/web-shared/js8.js` Worker harness (encode + decoder JS API) | DONE |
| 5 | `resources/web.qrc` aliases for `js8.{js,mjs,wasm}` (server build) | DONE |
| 5 | `tools/build-static.sh` already copies `wasm/*.mjs` + `wasm/*.wasm` (standalone) | DONE |

```
$ node tools/test-js8-wasm.mjs
encode("HELLOWK1FMab"): PASS
encode("CQK1FMEM85en"): PASS
decoder lifecycle: new=ok, push consumed=1000, free=ok
DAY 4 GATE: PASS
```

## Vendor diffs — full list

Six in-tree patches against pristine upstream JS8Call-improved
3f1b548. Each documented inline next to its change site.

| File | Change | Reason |
|---|---|---|
| `JS8_Mode/JS8.h` | Removed `<QObject>`/`<QSemaphore>`/`<QThread>` includes, `Q_NAMESPACE`, `class Decoder : public QObject`, `class Worker;` forward-decl | We do our own threading from JS Worker; the QObject Decoder is application-side glue |
| `JS8_Mode/JS8.cpp` | Removed `class Worker : public QObject`; renamed nested `Impl` class to top-level `DecoderImpl`; removed `JS8::Decoder::*` method bodies; removed `#include "JS8.moc"` | Same as above |
| `JS8_Mode/JS8.cpp` | `#include <boost/math/ccmath/round.hpp>` → constexpr round lambda inline | Emscripten 3.1.6's `boost-headers` port pins Boost 1.75; `ccmath` was added in 1.79 |
| `JS8_Mode/JS8.cpp` | `std::visit(generic-lambda, variant)` → manual `std::get_if` cascade | libc++14 (ships with emcc 3.1.6) has a return-type deduction bug in `std::visit`'s dispatch templates |
| `JS8_Mode/JS8.cpp` | Added `js8_make_decoder` / `js8_delete_decoder` / `js8_run_decoder` factory functions in `namespace JS8` | Lets `api/js8_wasm_api.cpp` cross the TU boundary without seeing `DecoderImpl`'s definition |
| `JS8_Main/Varicode.h` | Removed `class BuildMessageFramesThread : public QThread` (incl. signals/slots); added explicit `<QList>`/`<QMap>`/`<QPair>`/`<QSet>` includes upstream pulls in transitively | GUI threading wrapper we don't need; missing transitive includes the shim doesn't reproduce |
| `JS8_Main/Varicode.cpp` | Removed `BuildMessageFramesThread::ctor` + `run()` method bodies | Pairs with the .h removal |

The `_js8_*` factory functions and the std::visit / ccmath workarounds
will keep needing to be re-applied on each upstream re-vendor until the
host emcc updates to a libc++ that ships full constexpr ccmath + the
fixed std::visit template machinery (Emscripten 3.1.50+ should be
clean on both).

## Qt shim — final surface (~25 headers)

```
QBitArray QByteArray QChar QDateTime QDebug QLatin1String QList
QLoggingCategory QMap QMutex QObject QPair QPointer
QRegularExpression QSet QString QStringBuilder QStringList
QStringView QtAlgorithms QtGlobal QtMath QThread QVector
```

QString alone has 17 of the 30+ methods Varicode.cpp uses, plus an
iterator class that dereferences to QChar (real-Qt semantics — needed
because Varicode does `(*it).toUpper()`). Total shim size: ~700 lines
across all headers.

## What's NOT done in Phase 0

Per PLAN.md — these belong to later phases:

- **Phase 1**: real RX path. Phase 0 confirms `js8_decoder_run` links
  and runs without crashing on silence. It probably emits zero
  `Decoded` events because the input is silence; fixing the actual
  decode quality against captured JS8Call transmissions is Phase 1's
  whole 2–3 weeks.
- **Phase 2**: TX integration into wfweb's existing audio pipeline.
- **Phase 4**: UI panel (DIGI panel + free-text RX/TX + COI list).
- **Free-text Tier 2**: JSC compression dictionary stubbed out for now.
  Free-text frames will decode as empty strings until JSC tables are
  vendored (which is a +14 MB source / +1.5–3 MB WASM step).

## Tier 2 — JSC free-text dictionary (DONE, 2026-05-10)

Vendored upstream's full `JS8_JSC/{JSC.cpp,JSC.h,JSC_list.cpp,JSC_map.cpp}`
(262 144-entry English Huffman dictionary). Two surgical drops in the
Qt includes (`<QTextStream>` from JSC.h, `<QCache>` from JSC.cpp — both
unused); two shim additions (`QString::split(QString,SplitBehavior)`,
`QVector::removeFirst/removeLast/removeAt`); `QString::toLatin1()` now
returns a `QByteArray` (matches real-Qt; only callsite needed `.data()`).

Build cost: WASM 1.0 MB → 5.9 MB after JSC tables, 5.9 MB → 9.1 MB
after also exporting `js8_pack` (which transitively pulls in much more
of Varicode through buildMessageFrames + JSC::compress). Compile time
~75 s (most of which is parsing the 14 MB of `static const Tuple[]`
initialisers).

Roundtrip gate (`tools/test-js8-jsc-roundtrip.mjs`) — all four phrases
pack→encode→synth→decode→decompress losslessly:

```
"HELLO"                     → "HELLO"
"HELLO WORLD"               → "HELLO WORLD"
"WX FB ANT IS OK"           → "WX FB ANT IS OK"
"GOOD MORNING FROM BOSTON"  → "GOOD MORNING FROM BOSTON"
```

## Phase 0 timing

| | PLAN.md estimate | Actual |
|---|---|---|
| Day 1 (vendor) | 1 day | <1 day |
| Day 2 (shim) | 1 day | most of a day, mainly iterating on Varicode.cpp's surface |
| Day 3 (FFTW) | 1 day | <1 day; build is 90 seconds + cached |
| Day 4 (build + C API) | 1–2 days | most of a day; the iteration was Varicode.cpp's full Qt surface, not the build chain |
| Day 5 (worker harness) | 1 day | quick — just glue once the C API was settled |
| **Total** | 3–5 days | ~5 days |

Phase 1's 2–3 weeks should hold from here.
