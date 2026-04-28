#include "civemulator.h"

#include <QDebug>
#include <cmath>

civEmulator::civEmulator(quint8 radioCiv, QObject* parent)
    : QObject(parent), radioCiv(radioCiv)
{
    // Plausible defaults for the gains so the UI shows something sane.
    gains[1] = 128; // AF
    gains[2] = 255; // RF
    gains[3] = 64;  // Squelch
    gains[6] = 128; // Mic
    gains[7] = 128; // Comp
    gains[0x0A] = 128; // Power
}

void civEmulator::setSMeterFromPeak(quint16 peak)
{
    // Icom S-meter value is 0..255 BCD (0x0000..0x0255 in BCD4); 0 = S0,
    // 0x0120 ≈ S9, 0x0241 ≈ +60 dB. Map PCM peak (0..32767) logarithmically
    // so a loud voice lands near S9 and silence stays near S0.
    if (peak == 0) { sMeter = 0; }
    else {
        double db = 20.0 * std::log10((double)peak / 32767.0); // -∞..0 dBFS
        if (db < -60.0) db = -60.0;
        int v = (int)((db + 60.0) / 60.0 * 241.0);
        if (v < 0) v = 0;
        if (v > 241) v = 241;
        sMeter = (quint16)v;
    }
    // Push an unsolicited S-meter frame so the UI reflects live RX level even
    // when the client isn't actively polling 0x15 0x02.
    QByteArray pl;
    pl.append((char)0x15);
    pl.append((char)0x02);
    pl.append(u16ToBcd2(ptt ? 0 : sMeter));
    emit replyFrame(buildFrame(ctlCiv, pl));
}

void civEmulator::setTxMeterFromPeak(quint16 peak)
{
    // During TX, map mic peak 0..32767 log-scale to Po 0..255 (S0..S9+60 range).
    // Use same curve as the S-meter so UI movement is visually comparable.
    if (!ptt) { poMeter = 0; alcMeter = 0; swrMeter = 0; return; }
    if (peak == 0) { poMeter = 0; alcMeter = 0; }
    else {
        double db = 20.0 * std::log10((double)peak / 32767.0);
        if (db < -60.0) db = -60.0;
        int v = (int)((db + 60.0) / 60.0 * 213.0); // 213 = full scale on Po
        if (v < 0) v = 0;
        if (v > 213) v = 213;
        poMeter = (quint16)v;
        // ALC tracks Po but caps at ~120 (mid-scale) like a real rig at nominal drive.
        alcMeter = (quint16)(v * 120 / 213);
    }
    swrMeter = 0; // dummy load — SWR stays at 0.

    // Unsolicited Po update so the UI reflects TX activity without polling.
    QByteArray pl;
    pl.append((char)0x15);
    pl.append((char)0x11);
    pl.append(u16ToBcd2(poMeter));
    emit replyFrame(buildFrame(ctlCiv, pl));
}

QByteArray civEmulator::buildFrame(quint8 to, const QByteArray& payload) const
{
    QByteArray out;
    out.append((char)0xFE);
    out.append((char)0xFE);
    out.append((char)to);
    out.append((char)radioCiv);
    out.append(payload);
    out.append((char)0xFD);
    return out;
}

QByteArray civEmulator::ack(bool ok) const
{
    QByteArray p;
    p.append((char)(ok ? 0xFB : 0xFA));
    return buildFrame(ctlCiv, p);
}

// Icom little-endian BCD: byte 0 = low pair (10 Hz), byte 4 = high pair (GHz).
QByteArray civEmulator::freqToBcd5(quint64 hz) const
{
    QByteArray out(5, 0);
    for (int i = 0; i < 5; ++i) {
        int lo = hz % 10; hz /= 10;
        int hi = hz % 10; hz /= 10;
        out[i] = (char)((hi << 4) | lo);
    }
    return out;
}

quint64 civEmulator::bcd5ToFreq(const QByteArray& bcd) const
{
    quint64 f = 0;
    quint64 mult = 1;
    for (int i = 0; i < bcd.size() && i < 5; ++i) {
        quint8 b = (quint8)bcd[i];
        f += (b & 0x0F) * mult; mult *= 10;
        f += ((b >> 4) & 0x0F) * mult; mult *= 10;
    }
    return f;
}

QByteArray civEmulator::u16ToBcd2(quint16 v) const
{
    if (v > 9999) v = 9999;
    QByteArray out(2, 0);
    int d0 = v % 10;
    int d1 = (v / 10) % 10;
    int d2 = (v / 100) % 10;
    int d3 = (v / 1000) % 10;
    out[0] = (char)((d3 << 4) | d2);
    out[1] = (char)((d1 << 4) | d0);
    return out;
}

quint16 civEmulator::bcd2ToU16(const QByteArray& b) const
{
    if (b.size() < 2) return 0;
    quint8 hi = (quint8)b[0];
    quint8 lo = (quint8)b[1];
    return ((hi >> 4) & 0x0F) * 1000 + (hi & 0x0F) * 100
         + ((lo >> 4) & 0x0F) * 10   + (lo & 0x0F);
}

void civEmulator::emitTransceive(const QByteArray& payload)
{
    // Broadcast address 0x00 is the "transceive" update all controllers watch.
    emit replyFrame(buildFrame(0x00, payload));
}

void civEmulator::onCivFromClient(const QByteArray& frame)
{
    // Frame: FE FE <to> <from> <cmd> [<subcmd>] [<data>...] FD
    int start = frame.indexOf(QByteArray("\xFE\xFE", 2));
    int end = frame.lastIndexOf((char)0xFD);
    if (start < 0 || end < 0 || end <= start + 4) {
        return;
    }

    const int p = start + 2;
    // const quint8 to = (quint8)frame[p + 0]; // should equal radioCiv
    const quint8 from = (quint8)frame[p + 1];
    ctlCiv = from;
    const quint8 cmd = (quint8)frame[p + 2];
    const QByteArray body = frame.mid(p + 3, end - (p + 3));

    qDebug().noquote() << QString("civ[%1] rx cmd=0x%2 body=%3 full=%4")
        .arg(name)
        .arg(cmd, 2, 16, QChar('0'))
        .arg(QString::fromLatin1(body.toHex(' ')))
        .arg(QString::fromLatin1(frame.toHex(' ')));

    switch (cmd) {
    case 0x03: { // read freq
        QByteArray pl;
        pl.append((char)0x03);
        pl.append(freqToBcd5(freq));
        emit replyFrame(buildFrame(ctlCiv, pl));
        break;
    }
    case 0x04: { // read mode
        QByteArray pl;
        pl.append((char)0x04);
        pl.append((char)mode);
        pl.append((char)filter);
        emit replyFrame(buildFrame(ctlCiv, pl));
        break;
    }
    case 0x05: { // set freq (5 BCD bytes follow)
        if (body.size() >= 5) {
            freq = bcd5ToFreq(body.left(5));
            emit replyFrame(ack(true));
            QByteArray t;
            t.append((char)0x00);
            t.append(freqToBcd5(freq));
            emitTransceive(t); // cmd 0x00 = transceive freq
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x06: { // set mode (mode[, filter])
        if (body.size() >= 1) {
            mode = (quint8)body[0];
            if (body.size() >= 2) filter = (quint8)body[1];
            emit replyFrame(ack(true));
            QByteArray t;
            t.append((char)0x01);
            t.append((char)mode);
            t.append((char)filter);
            emitTransceive(t); // cmd 0x01 = transceive mode
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x25: { // Selected/Unselected freq (with VFO prefix: 0x00=selected, 0x01=unselected)
        if (body.size() == 1) {
            quint8 vfo = (quint8)body[0];
            QByteArray pl;
            pl.append((char)0x25);
            pl.append((char)vfo);
            pl.append(freqToBcd5(freq));
            emit replyFrame(buildFrame(ctlCiv, pl));
        } else if (body.size() >= 6) {
            freq = bcd5ToFreq(body.mid(1, 5));
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x26: { // Selected/Unselected mode: read = 1-byte VFO, write = VFO+mode+data+filter
        if (body.size() == 1) {
            quint8 vfo = (quint8)body[0];
            QByteArray pl;
            pl.append((char)0x26);
            pl.append((char)vfo);
            pl.append((char)mode);
            pl.append((char)dataMode);
            pl.append((char)filter);
            emit replyFrame(buildFrame(ctlCiv, pl));
        } else if (body.size() >= 4) {
            mode = (quint8)body[1];
            dataMode = (quint8)body[2];
            filter = (quint8)body[3];
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x14: { // gain read/write
        if (body.size() == 1) {
            quint8 sub = (quint8)body[0];
            QByteArray pl;
            pl.append((char)0x14);
            pl.append((char)sub);
            pl.append(u16ToBcd2(gains[sub & 0x0F]));
            emit replyFrame(buildFrame(ctlCiv, pl));
        } else if (body.size() >= 3) {
            quint8 sub = (quint8)body[0];
            gains[sub & 0x0F] = bcd2ToU16(body.mid(1, 2));
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x15: { // read meters
        if (body.size() >= 1) {
            quint8 sub = (quint8)body[0];
            quint16 val = 0;
            bool known = true;
            switch (sub) {
            case 0x02: val = ptt ? 0 : sMeter; break;   // S-meter
            case 0x11: val = poMeter;          break;   // Po (TX output)
            case 0x12: val = swrMeter;         break;   // SWR
            case 0x13: val = alcMeter;         break;   // ALC
            default: known = false; break;
            }
            if (known) {
                QByteArray pl;
                pl.append((char)0x15);
                pl.append((char)sub);
                pl.append(u16ToBcd2(val));
                emit replyFrame(buildFrame(ctlCiv, pl));
            } else {
                emit replyFrame(ack(true));
            }
        } else {
            emit replyFrame(ack(true));
        }
        break;
    }
    case 0x19: { // read civ address
        if (body.size() >= 1 && (quint8)body[0] == 0x00) {
            QByteArray pl;
            pl.append((char)0x19);
            pl.append((char)0x00);
            pl.append((char)radioCiv);
            emit replyFrame(buildFrame(ctlCiv, pl));
        } else {
            emit replyFrame(ack(true));
        }
        break;
    }
    case 0x1C: { // misc controls; 0x00 = PTT
        if (body.size() >= 1 && (quint8)body[0] == 0x00) {
            if (body.size() == 1) {
                // PTT read
                QByteArray pl;
                pl.append((char)0x1C);
                pl.append((char)0x00);
                pl.append((char)(ptt ? 0x01 : 0x00));
                emit replyFrame(buildFrame(ctlCiv, pl));
            } else {
                // PTT write
                bool newPtt = ((quint8)body[1] != 0);
                if (newPtt != ptt) {
                    ptt = newPtt;
                    emit pttChanged(ptt);
                }
                emit replyFrame(ack(true));
            }
        } else {
            emit replyFrame(ack(true));
        }
        break;
    }
    default:
        // 0x27 (scope) is polled aggressively by wfweb; we don't synthesise
        // FFT frames yet, so demote it to debug to keep the log readable.
        if (cmd == 0x27) {
            qDebug().noquote() << QString("  -> unhandled cmd 0x%1, sending ACK")
                .arg(cmd, 2, 16, QChar('0'));
        } else {
            qInfo().noquote() << QString("  -> unhandled cmd 0x%1, sending ACK")
                .arg(cmd, 2, 16, QChar('0'));
        }
        emit replyFrame(ack(true));
        break;
    }
}
