#ifndef CIVRAW_H
#define CIVRAW_H

#include <QByteArray>
#include <QList>
#include <QString>

// Raw CI-V passthrough for the web layer (POST /api/v1/radio/civ, WebSocket
// civRaw).  Pure Qt5Core: parsing, validation and framing only; webServer
// owns the queue and hands the frame to rigCommander::dataFromServer, the
// path the rig server and the CI-V TCP port already use.
//
// Frames go out from their own controller address, kCtrlAddr, so the radio's
// reply is addressed to it and nobody else: icomCommander::parseData takes
// only frames to E0 / E1 / 00, so a raw reply never reaches the cache, and an
// NG to a raw command never counts toward its "five FAs = radio off" rule.
namespace CivRaw {

constexpr quint8 kCtrlAddr = 0xE2;
constexpr int kMaxBody = 64;   // command + data bytes, without FE FE to from / FD

// Reads "1A 05 00 61" (a bare command body) or a whole frame
// "FE FE 94 E0 1A 05 00 61 FD".  Spaces, ':' ',' '-' between bytes are
// ignored.  A whole frame must be addressed to rigAddr; its from-address is
// replaced by kCtrlAddr.  Exactly one frame: no FE or FD inside the body.
// Returns false with *error set when the input is not one well-formed frame.
bool parse(const QString &hex, quint8 rigAddr, QByteArray *body, QString *error);

// Why this body is not taken, or an empty string.  Raw CI-V would bypass
// TX inhibit, the compressor hold and the PTT bookkeeping, so commands that
// transmit (1C 00 PTT, 1C 01 tuner / TUNE, 17 CW) and power control (18) are
// refused; the named endpoints exist for those.
QString refusedReason(const QByteArray &body);

// FE FE rigAddr kCtrlAddr body FD
QByteArray frame(quint8 rigAddr, const QByteArray &body);

// Moves every complete FE FE ... FD frame out of *buf, leaving a partial
// tail (and dropping bytes before the first FE FE).
QList<QByteArray> takeFrames(QByteArray *buf);

// True for a frame from rigAddr to kCtrlAddr: the reply to our request.
bool isReply(const QByteArray &frame, quint8 rigAddr);

// "FE FE 94 E2 1A FD": upper-case hex, one space between bytes.
QString toHex(const QByteArray &bytes);

} // namespace CivRaw

#endif // CIVRAW_H
