// SPIKE-MINIMAL Varicode.h — just the methods + enum constants that
// DecodedText.cpp references. Method bodies are stubs in Varicode.cpp.
#pragma once
#include "QtGlobal"
#include "QString"
#include "QStringList"

class Varicode {
public:
    enum SubmodeType {
        JS8CallNormal = 0,
        JS8CallFast = 1,
        JS8CallTurbo = 2,
        JS8CallSlow = 4,
        JS8CallUltra = 8
    };

    enum TransmissionType {
        JS8Call = 0,
        JS8CallFirst = 1,
        JS8CallLast = 2,
        JS8CallData = 4,
    };

    enum FrameType {
        FrameUnknown = 255,
        FrameHeartbeat = 0,
        FrameCompound = 1,
        FrameCompoundDirected = 2,
        FrameDirected = 3,
        FrameData = 4,
        FrameDataCompressed = 6,
    };

    static QString cqString(int number);
    static QString hbString(int number);

    // The 5 unpack methods DecodedText calls
    static QString unpackFastDataMessage(const QString& text);
    static QString unpackDataMessage(const QString& text);
    static QStringList unpackHeartbeatMessage(const QString& text, quint8* pType,
                                              bool* pIsAlt, quint8* pBits3);
    static QStringList unpackCompoundMessage(const QString& text, quint8* pType,
                                             quint8* pBits3);
    static QStringList unpackDirectedMessage(const QString& text, quint8* pType);
};
