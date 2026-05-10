# JS8 Qt-shim spike

Verifies that JS8Call-improved/JS8_Mode/DecodedText.cpp compiles + runs
verbatim against a hand-written ~270-line Qt-string shim — no Qt
runtime, no Qt build system. Path A from the spike report.

## Layout

    shim/                  ← the actual shim (this is what we keep)
      QtGlobal             ← typedefs (quint*/qint*/qreal)
      QChar                ← char32_t wrapper
      QString              ← std::string-backed, 17 methods + arg()
      QStringList          ← std::vector<QString> wrapper
      QStringBuilder       ← placeholder (operator% aliased to + in QString)
    vendor/                ← inputs to the spike
      DecodedText.{cpp,h}  ← real source from JS8Call-improved (UNCHANGED)
      JS8.h                ← stripped to just JS8::Event::Decoded etc, no QObject
      Varicode.h           ← stripped to just the 5 unpack methods + enums
      Varicode_stub.cpp    ← returns empty/sentinels — real impls come in port
      commons.h            ← real one (no Qt)
    test_decoded.cpp       ← constructs a DecodedText, prints fields

## Build native

    g++ -std=c++20 -O2 -I shim -I vendor \
        vendor/DecodedText.cpp vendor/Varicode_stub.cpp test_decoded.cpp \
        -o test_decoded_native
    ./test_decoded_native

## Build wasm

    emcc -std=c++20 -O2 -fno-exceptions -I shim -I vendor \
        vendor/DecodedText.cpp vendor/Varicode_stub.cpp test_decoded.cpp \
        -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 \
        -s EXPORT_NAME=createDecodedText \
        -s ALLOW_MEMORY_GROWTH=1 \
        -o test_decoded.mjs

## Result

Compiles + runs. 34 KB .wasm. Reproduces every QString operation
DecodedText.cpp uses (printf-style arg, QStringBuilder concat, split/join,
QStringList::value/mid).

## What's still missing for a real port

1. Varicode unpack methods need real implementations (~180 lines from upstream).
   Will likely add 5-10 more QString methods (toUpper/toLower/replace/startsWith).
2. JS8.cpp's DSP path (FFTW + Eigen + Boost::multi_index) needs a separate
   compile spike — same C++, same shim, but with FFTW vendored under emcc.
3. JSC (free-text Huffman dictionary) is a 14 MB code-generated table —
   defer to a later phase.
