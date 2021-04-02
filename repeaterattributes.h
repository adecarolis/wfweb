#ifndef REPEATERATTRIBUTES_H
#define REPEATERATTRIBUTES_H
#include <QMetaType>

enum duplexMode {
    dmSplitOff=0x00,
    dmSplitOn=0x01,
    dmSimplex=0x10,
    dmDupMinus=0x11,
    dmDupPlus=0x12,
    dmDupRPS=0x13,
    dmDupAutoOn=0x26,
    dmDupAutoOff=0x36
};

Q_DECLARE_METATYPE(enum duplexMode)

#endif // REPEATERATTRIBUTES_H
