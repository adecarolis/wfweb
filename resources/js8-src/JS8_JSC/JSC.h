#ifndef JSC_H
#define JSC_H

/**
 * (C) 2018 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 **/

#include <QList>
#include <QMap>
#include <QPair>
#include <QStringList>
#include <QVector>
// wfweb vendor diff: QTextStream removed — only the #if 0 loadCompressionTable
// overload referenced it, and the qt-shim has no QTextStream.

typedef QPair<QVector<bool>, quint32>
    CodewordPair; // Tuple(Codeword, N) where N = number of characters
typedef QVector<bool> Codeword; // Codeword bit vector

typedef struct Tuple {
    char const *str;
    int size;
    int index;
} Tuple;

class JSC {
  public:
#if 0
    static CompressionTable loadCompressionTable();
    static CompressionTable loadCompressionTable(QTextStream &stream);
#endif
    static Codeword codeword(quint32 index, bool separate, quint32 bytesize,
                             quint32 s, quint32 c);
    static QList<CodewordPair> compress(QString text);
    static QString decompress(Codeword const &bits);

    static bool exists(QString w, quint32 *pIndex);
    static quint32 lookup(QString w, bool *ok);
    static quint32 lookup(char const *b, bool *ok);

    static const quint32 size = 262144;
    static const Tuple map[262144];
    static const Tuple list[262144];

    static const quint32 prefixSize = 103;
    static const Tuple prefix[103];
};

#endif // JSC_H
