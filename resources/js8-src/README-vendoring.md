# JS8 codec — vendored from JS8Call-improved

Source: <https://github.com/JS8Call-improved/JS8Call-improved>
Pinned commit: `3f1b548965a45d41eaae57b61a23c2f42fc8d4cc` (2026-05-09)
License: GPLv3 (compatible with wfweb)

This tree contains the subset of JS8Call-improved that wfweb's WASM JS8
module compiles. Application glue — Qt UI, Qt threading, audio I/O,
network, logbook, transceiver — is **not** here; wfweb supplies its own
SPA, audio bus, PTT path, and rig control.

## What's kept

Every file is verbatim from upstream **unless explicitly listed below as
modified**. Diffs are intentionally tiny so a future re-vendor against a
newer upstream is mostly `cp`.

| Path | Upstream path | Modified? |
|---|---|---|
| `JS8_Mode/JS8.cpp` | `JS8_Mode/JS8.cpp` | **yes** — see "Diffs from upstream" |
| `JS8_Mode/JS8.h` | `JS8_Mode/JS8.h` | **yes** |
| `JS8_Mode/JS8Submode.{cpp,h}` | same | no |
| `JS8_Mode/DecodedText.{cpp,h}` | same | no |
| `JS8_Mode/FrequencyTracker.{cpp,h}` | same | no |
| `JS8_Mode/ldpc_feedback.h` | same | no |
| `JS8_Mode/soft_combiner.h` | same | no |
| `JS8_Mode/whitening_processor.h` | same | no |
| `JS8_Main/Varicode.{cpp,h}` | same | no |
| `JS8_Main/DriftingDateTime.h` | same | no |
| `JS8_Include/commons.h` | same | no |
| `JS8_JSC/JSC.h` | same | **yes** — `<QTextStream>` include dropped |
| `JS8_JSC/JSC.cpp` | same | **yes** — `<QCache>` include dropped |
| `JS8_JSC/JSC_list.cpp` | same | no (~7 MB, 262 144 dictionary entries) |
| `JSC_JSC/JSC_map.cpp` | same | no (~7 MB, indexed dictionary) |
| `vendor/CRCpp/CRC.h` | `vendor/CRCpp/CRC.h` | no |
| `vendor/Eigen/` | `vendor/Eigen/` | no |

## What's deliberately not vendored

| Upstream area | Why we don't need it |
|---|---|
| `JS8_Mode/Decoder.{cpp,h}` | QObject wrapper for QThread orchestration. The WASM bridge does its own threading via Web Workers; the underlying decode logic lives inside `JS8.cpp`'s class (renamed `DecoderImpl` in our tree). |
| `JS8_Mode/Modulator.{cpp,h}` | QAudio output glue. We synthesize 8-FSK from the 79-tone array in `js8.js` and feed it through wfweb's existing TX path. |
| `JS8_Mode/Detector.{cpp,h}` | QAudio input glue, same reasoning. |
| `JS8_Audio/`, `JS8_UI/`, `JS8_Mainwindow/`, `JS8_Network/`, `JS8_Logbook/`, `JS8_Transceiver/`, `JS8_UDP/`, `JS8_Widgets/` | Application code. |
| `JS8_JSC/JSC_checker.{cpp,h}` | GUI spell-check (QTextEdit/QTextCursor); compress/decompress doesn't reference it. |
| `vendor/sqlite3/` | Logbook persistence. |
| `tools/`, `docker/`, `docs/`, `Palettes/`, `media/`, `icons/`, `artwork/` | Not codec. |

## Diffs from upstream

### `JS8_Mode/JS8.h`

- Removed `#include <QObject>`, `#include <QSemaphore>`, `#include <QThread>`.
- Removed the `Q_NAMESPACE` line.
- Removed `class Worker;` forward declaration.
- Removed `class Decoder : public QObject { Q_OBJECT … };`.

Kept everything else: `namespace JS8`, the `Costas::*` constexpr tables,
the `encode()` declaration, the `Event::{DecodeStarted,SyncStart,SyncState,
Decoded,DecodeFinished,Variant,Emitter}` types.

### `JS8_Mode/JS8.cpp`

- The Worker QObject (with `Q_OBJECT`, `signals`, `public slots`, `run()`,
  `stop()`, `copy()`, `m_semaphore`, `m_quit`) is gone.
- Worker's nested `Impl` class is hoisted out and renamed `DecoderImpl`
  (same internals: `dec_data &m_data`, `DecodeEntry`, `m_decodes`,
  `operator()(Event::Emitter)`).
- `#include "JS8.moc"` removed (no QObject in this TU).
- `JS8::Decoder::Decoder()`, `start()`, `quit()`, `decode()` method
  bodies removed.

`Q_DECLARE_LOGGING_CATEGORY(decoder_js8);` and the `qCDebug(decoder_js8) <<
…` calls remain; the qt-shim provides no-op stand-ins for these so we keep
upstream's diagnostic surface intact.

### `JS8_JSC/JSC.h`

- Dropped `#include <QTextStream>` — only the `#if 0` `loadCompressionTable`
  overload referenced it. Replaced with a comment.

### `JS8_JSC/JSC.cpp`

- Dropped `#include <QCache>` — `LOOKUP_CACHE` is a `QMap`, not a `QCache`.
  Replaced with a comment.

## How to refresh this tree against a newer upstream

1. `git clone https://github.com/JS8Call-improved/JS8Call-improved.git /tmp/js8-upstream`
2. Note the new commit SHA; update this file's frontmatter.
3. `cp` each file in the table above. The "no-modification" rows go
   straight in.
4. For `JS8.h` and `JS8.cpp`, redo the diffs documented above (search and
   delete the listed blocks). The diff is small and stable — upstream
   touches the codec internals far more than the threading wrapper.
5. Run `tools/build-js8-wasm.sh` and `node test_decoded.cjs` (the spike
   harness, when promoted to a real test) to confirm nothing regressed.
