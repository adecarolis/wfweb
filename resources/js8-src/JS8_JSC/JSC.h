// Tier 2 stub. Upstream's JS8_JSC contains the JS8 free-text Huffman
// dictionary (262 144-entry English wordlist used to compress chat
// messages into 87 bits). Vendoring the real tables means +14 MB of
// generated source and +1.5–3 MB of WASM, so it's deferred to a later
// phase per PLAN.md.
//
// This header declares + implements API-compatible stubs that return
// "empty" everywhere. Free-text decoding will return empty strings;
// compressed-frame encoding will refuse. Structured frames (CQ,
// directed messages, heartbeats, compound callsigns) are unaffected.
#pragma once
#include "QtGlobal"
#include "QPair"
#include "QString"
#include "QStringList"
#include "QVector"

using Codeword = QVector<bool>;
using CodewordPair = QPair<QVector<bool>, quint32>;

class JSC {
public:
    static const quint32 size = 262144;
    static const quint32 prefixSize = 103;

    static QList<CodewordPair> compress(QString /*text*/) {
        return QList<CodewordPair>{};
    }
    static QString decompress(Codeword const& /*bits*/) {
        return QString();
    }
    static bool exists(QString /*w*/, quint32* pIndex = nullptr) {
        if (pIndex) *pIndex = 0;
        return false;
    }
    static quint32 lookup(QString /*w*/, bool* ok = nullptr) {
        if (ok) *ok = false;
        return 0;
    }
    static quint32 lookup(char const* /*b*/, bool* ok = nullptr) {
        if (ok) *ok = false;
        return 0;
    }
    static Codeword codeword(quint32 /*index*/, bool /*separate*/,
                             quint32 /*bytesize*/, quint32 /*shift*/) {
        return Codeword{};
    }
};
