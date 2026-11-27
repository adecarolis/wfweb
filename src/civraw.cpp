#include "civraw.h"

namespace CivRaw {

static int hexNibble(QChar c)
{
    const ushort u = c.unicode();
    if (u >= '0' && u <= '9') return u - '0';
    if (u >= 'a' && u <= 'f') return u - 'a' + 10;
    if (u >= 'A' && u <= 'F') return u - 'A' + 10;
    return -1;
}

static bool isSeparator(QChar c)
{
    return c.isSpace() || c == QLatin1Char(':') || c == QLatin1Char(',') || c == QLatin1Char('-');
}

bool parse(const QString &hex, quint8 rigAddr, QByteArray *body, QString *error)
{
    QByteArray bytes;
    int hi = -1;
    for (const QChar c : hex) {
        if (isSeparator(c)) {
            if (hi >= 0) { *error = QStringLiteral("odd number of hex digits in a byte"); return false; }
            continue;
        }
        const int n = hexNibble(c);
        if (n < 0) { *error = QStringLiteral("not a hex digit: '%1'").arg(c); return false; }
        if (hi < 0) {
            hi = n;
        } else {
            bytes.append(static_cast<char>((hi << 4) | n));
            hi = -1;
        }
    }
    if (hi >= 0) { *error = QStringLiteral("odd number of hex digits"); return false; }
    if (bytes.isEmpty()) { *error = QStringLiteral("empty command"); return false; }

    QByteArray b;
    if (static_cast<quint8>(bytes.at(0)) == 0xFE) {
        // A whole frame: FE FE to from <body> FD
        if (bytes.size() < 6 || static_cast<quint8>(bytes.at(1)) != 0xFE
            || static_cast<quint8>(bytes.at(bytes.size() - 1)) != 0xFD) {
            *error = QStringLiteral("a frame must be FE FE <to> <from> <command> ... FD");
            return false;
        }
        const quint8 to = static_cast<quint8>(bytes.at(2));
        if (to != rigAddr) {
            *error = QStringLiteral("frame is addressed to %1, the radio is %2")
                         .arg(to, 2, 16, QLatin1Char('0')).arg(rigAddr, 2, 16, QLatin1Char('0')).toUpper();
            return false;
        }
        b = bytes.mid(4, bytes.size() - 5);
    } else {
        b = bytes;
    }
    if (b.isEmpty()) { *error = QStringLiteral("empty command"); return false; }
    if (b.size() > kMaxBody) {
        *error = QStringLiteral("command longer than %1 bytes").arg(kMaxBody);
        return false;
    }
    for (const char ch : b) {
        const quint8 u = static_cast<quint8>(ch);
        if (u == 0xFE || u == 0xFD) {
            *error = QStringLiteral("FE / FD inside the command: one frame per request");
            return false;
        }
    }
    *body = b;
    return true;
}

QString refusedReason(const QByteArray &bodyIn)
{
    QByteArray b = bodyIn;
    // 29 xx <command>: the receiver prefix of the dual-receiver rigs.
    if (b.size() >= 3 && static_cast<quint8>(b.at(0)) == 0x29)
        b = b.mid(2);
    if (b.isEmpty())
        return QString();
    const quint8 cmd = static_cast<quint8>(b.at(0));
    if (cmd == 0x1C && b.size() >= 2) {
        const quint8 sub = static_cast<quint8>(b.at(1));
        if (sub == 0x00) return QStringLiteral("1C 00 (PTT) is not taken here: use the PTT control");
        if (sub == 0x01) return QStringLiteral("1C 01 (tuner / TUNE) is not taken here: use the tuner control");
    }
    if (cmd == 0x17) return QStringLiteral("17 (CW send) is not taken here: use /api/v1/radio/cw");
    if (cmd == 0x18) return QStringLiteral("18 (power on / off) is not taken here: use the power control");
    return QString();
}

QByteArray frame(quint8 rigAddr, const QByteArray &body)
{
    QByteArray f;
    f.reserve(body.size() + 5);
    f.append('\xFE');
    f.append('\xFE');
    f.append(static_cast<char>(rigAddr));
    f.append(static_cast<char>(kCtrlAddr));
    f.append(body);
    f.append('\xFD');
    return f;
}

QList<QByteArray> takeFrames(QByteArray *buf)
{
    QList<QByteArray> out;
    for (;;) {
        const int start = buf->indexOf(QByteArray("\xFE\xFE", 2));
        if (start < 0) {
            // Keep a lone trailing FE: it may be the first half of a preamble.
            if (!buf->isEmpty() && static_cast<quint8>(buf->at(buf->size() - 1)) == 0xFE)
                *buf = buf->right(1);
            else
                buf->clear();
            break;
        }
        // Skip a run of FE (some rigs send FE FE FE as the preamble).
        int bodyStart = start + 2;
        while (bodyStart < buf->size() && static_cast<quint8>(buf->at(bodyStart)) == 0xFE)
            ++bodyStart;
        const int end = buf->indexOf('\xFD', bodyStart);
        if (end < 0) {
            buf->remove(0, start);
            break;
        }
        QByteArray f("\xFE\xFE", 2);
        f.append(buf->mid(bodyStart, end - bodyStart + 1));
        buf->remove(0, end + 1);
        out.append(f);
    }
    return out;
}

bool isReply(const QByteArray &f, quint8 rigAddr)
{
    return f.size() >= 6
        && static_cast<quint8>(f.at(2)) == kCtrlAddr
        && static_cast<quint8>(f.at(3)) == rigAddr;
}

QString toHex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

} // namespace CivRaw
