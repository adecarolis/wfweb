// Day-2 gate: every shim header must compile when included from
// arbitrary order, and the codec subset's headers must syntax-check
// against the shim. No runtime — just a compile probe.
//
//   g++ -std=c++20 -fsyntax-only -I resources/js8-src/qt-shim \
//       -I resources/js8-src -I resources/js8-src/vendor \
//       resources/js8-src/qt-shim/_smoketest.cpp

// -- Every shim header in alphabetical order
#include "QBitArray"
#include "QByteArray"
#include "QChar"
#include "QDateTime"
#include "QDebug"
#include "QList"
#include "QLoggingCategory"
#include "QMap"
#include "QMutex"
#include "QObject"
#include "QPair"
#include "QPointer"
#include "QRegularExpression"
#include "QSet"
#include "QString"
#include "QStringBuilder"
#include "QStringList"
#include "QStringView"
#include "QtAlgorithms"
#include "QtGlobal"
#include "QtMath"
#include "QThread"
#include "QVector"

// -- Headers from the codec subset
#include "JS8_Include/commons.h"
#include "JS8_Mode/JS8.h"
#include "JS8_Mode/JS8Submode.h"
#include "JS8_Mode/DecodedText.h"
#include "JS8_Mode/FrequencyTracker.h"
#include "JS8_Mode/ldpc_feedback.h"
#include "JS8_Mode/soft_combiner.h"
#include "JS8_Mode/whitening_processor.h"
#include "JS8_Main/Varicode.h"

// Ensure the linker sees us reach the symbol-level too:
int main() {
    QString s = QStringLiteral("hello");
    QStringList parts = s.split(QChar(','));
    QByteArray ba = s.toLocal8Bit();
    QVector<int> v{1, 2, 3};
    QMap<QString, int> m{{QStringLiteral("a"), 1}};
    QSet<int> set{1, 2};
    QPair<int, int> p = qMakePair(1, 2);
    QBitArray bits(8);
    QRegularExpression re(QStringLiteral("\\w+"));
    auto match = re.match(s);
    (void)parts; (void)ba; (void)v; (void)m; (void)set; (void)p;
    (void)bits; (void)match;
    return 0;
}
