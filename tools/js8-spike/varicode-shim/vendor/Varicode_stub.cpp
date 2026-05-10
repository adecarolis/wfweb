// SPIKE-MINIMAL Varicode stubs — just enough symbols for DecodedText to link.
// Real implementations come in phase 0 of the actual port.
#include "Varicode.h"

QString Varicode::cqString(int) { return QString("CQ"); }
QString Varicode::hbString(int) { return QString("HB"); }

QString Varicode::unpackFastDataMessage(const QString&) { return QString(); }
QString Varicode::unpackDataMessage(const QString&)     { return QString(); }

QStringList Varicode::unpackHeartbeatMessage(const QString&, quint8* pType,
                                             bool* pIsAlt, quint8* pBits3) {
    if (pType)  *pType  = FrameUnknown;
    if (pIsAlt) *pIsAlt = false;
    if (pBits3) *pBits3 = 0;
    return QStringList{};
}
QStringList Varicode::unpackCompoundMessage(const QString&, quint8* pType,
                                            quint8* pBits3) {
    if (pType)  *pType  = FrameUnknown;
    if (pBits3) *pBits3 = 0;
    return QStringList{};
}
QStringList Varicode::unpackDirectedMessage(const QString&, quint8* pType) {
    if (pType) *pType = FrameUnknown;
    return QStringList{};
}
