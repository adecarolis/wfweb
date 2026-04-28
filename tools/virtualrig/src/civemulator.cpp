#include "civemulator.h"

#include <QDebug>
#include <QRandomGenerator>
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

    scopeTimer = new QTimer(this);
    scopeTimer->setInterval(50); // 20 fps
    connect(scopeTimer, &QTimer::timeout, this, &civEmulator::emitScopeWaveData);
}

void civEmulator::setExternalPtt(bool on)
{
    if (ptt == on) return;
    ptt = on;
    emit pttChanged(on);
    updateScopeTimerState(); // mute scope while keyed
}

void civEmulator::setSMeterFromPeak(quint16 peak)
{
    lastRxPeak = peak;
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
            quint16 v;
            switch (sub) {
            case 0x09: v = cwPitchEncoded;  break;  // CW pitch
            case 0x0C: v = keySpeedEncoded; break;  // key speed
            default:   v = gains[sub & 0x0F]; break;
            }
            QByteArray pl;
            pl.append((char)0x14);
            pl.append((char)sub);
            pl.append(u16ToBcd2(v));
            emit replyFrame(buildFrame(ctlCiv, pl));
        } else if (body.size() >= 3) {
            quint8 sub = (quint8)body[0];
            quint16 v = bcd2ToU16(body.mid(1, 2));
            switch (sub) {
            case 0x09: cwPitchEncoded  = (quint8)qBound<quint16>(0, v, 255); break;
            case 0x0C: keySpeedEncoded = (quint8)qBound<quint16>(0, v, 255); break;
            default:   gains[sub & 0x0F] = v; break;
            }
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x16: { // various toggles. Only 0x47 (break-in) is meaningful here.
        if (body.isEmpty()) {
            emit replyFrame(ack(true));
        } else if ((quint8)body[0] == 0x47) {
            if (body.size() == 1) {
                QByteArray pl;
                pl.append((char)0x16);
                pl.append((char)0x47);
                pl.append((char)breakInMode);
                emit replyFrame(buildFrame(ctlCiv, pl));
            } else {
                breakInMode = (quint8)body[1];
                emit replyFrame(ack(true));
            }
        } else {
            emit replyFrame(ack(true));
        }
        break;
    }
    case 0x17: { // send CW (no reply per IC-V ICD; ack anyway for safety)
        // Empty payload OR 0xFF means abort.
        bool isAbort = body.isEmpty() || ((quint8)body[0] == 0xFF);
        if (isAbort) {
            emit cwAbortRequested();
        } else {
            emit cwSendRequested(body, keySpeedWpm(), cwPitchHz());
        }
        // Real rigs return no reply for 0x17, but a benign ACK keeps the
        // client's queue happy (it doesn't strictly require one either).
        emit replyFrame(ack(true));
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
    case 0x27: { // scope (waterfall)
        handleScopeCommand(body);
        break;
    }
    default:
        qInfo().noquote() << QString("  -> unhandled cmd 0x%1, sending ACK")
            .arg(cmd, 2, 16, QChar('0'));
        emit replyFrame(ack(true));
        break;
    }
}

// ---------------------------------------------------------------------------
// 0x27 — Scope / waterfall.
//
// Sub-command map (IC-7300):
//   0x00  wave data    — pushed by rig, not requested
//   0x10  on/off
//   0x11  data output (master enable for periodic 0x00 frames)
//   0x13  single/dual
//   0x14  mode (0=center, 1=fixed, 2=scroll-C, 3=scroll-F) [+VFO]
//   0x15  span freq (5-byte BCD half-span, e.g. 5000 = ±5 kHz) [+VFO]
//   0x16  edge (1..3 BCD) [+VFO]
//   0x17  hold [+VFO]
//   0x19  reference level (3 bytes: BCD-hi, BCD-lo, sign) [+VFO]
//   0x1a  speed (0..2) [+VFO]
//   0x1b  during TX
//   0x1c  center type [+VFO]
//   0x1d  VBW [+VFO]
//   0x1e  fixed edge freq
//   0x1f  RBW [+VFO]
// READ frames carry just the cmd (and VFO if applicable); WRITE frames carry
// payload after that. We distinguish on body length.
// ---------------------------------------------------------------------------

namespace {
// IC-7300 spans: half-bandwidth in Hz, indexed 0..7. Matches the rig file.
constexpr quint64 kIcom7300Spans[8] = {
    2500, 5000, 10000, 25000, 50000, 100000, 250000, 500000
};
}

quint64 civEmulator::spanHzForIndex(quint8 idx) const
{
    if (idx >= 8) return 5000;
    return kIcom7300Spans[idx];
}

void civEmulator::updateScopeTimerState()
{
    if (!scopeTimer) return;
    const bool shouldRun = scopeOn && scopeDataOutput && !ptt;
    if (shouldRun && !scopeTimer->isActive()) scopeTimer->start();
    else if (!shouldRun && scopeTimer->isActive()) scopeTimer->stop();
}

void civEmulator::handleScopeCommand(const QByteArray& body)
{
    if (body.isEmpty()) {
        emit replyFrame(ack(true));
        return;
    }
    const quint8 sub = (quint8)body[0];
    const QByteArray rest = body.mid(1); // bytes after the subcmd

    auto echoSimple = [&](const QByteArray& value) {
        QByteArray pl;
        pl.append((char)0x27);
        pl.append((char)sub);
        pl.append(value);
        emit replyFrame(buildFrame(ctlCiv, pl));
    };
    // Reply for sub-commands that take a VFO byte (0x14, 0x15, 0x16, ...).
    auto echoVfo = [&](quint8 vfo, const QByteArray& value) {
        QByteArray pl;
        pl.append((char)0x27);
        pl.append((char)sub);
        pl.append((char)vfo);
        pl.append(value);
        emit replyFrame(buildFrame(ctlCiv, pl));
    };

    switch (sub) {
    case 0x00: { // wave data — also a one-shot read
        emitScopeWaveData();
        break;
    }
    case 0x10: { // scope on/off
        if (rest.isEmpty()) {
            QByteArray v(1, (char)(scopeOn ? 0x01 : 0x00));
            echoSimple(v);
        } else {
            scopeOn = ((quint8)rest[0] != 0);
            updateScopeTimerState();
            emit replyFrame(ack(true));
        }
        break;
    }
    case 0x11: { // scope data output (master enable for 0x27 0x00 frames)
        if (rest.isEmpty()) {
            QByteArray v(1, (char)(scopeDataOutput ? 0x01 : 0x00));
            echoSimple(v);
        } else {
            scopeDataOutput = ((quint8)rest[0] != 0);
            updateScopeTimerState();
            emit replyFrame(ack(true));
            // Real rigs push one frame immediately on enable so the client
            // doesn't have to wait a full tick for the first paint.
            if (scopeDataOutput && scopeOn && !ptt) emitScopeWaveData();
        }
        break;
    }
    case 0x13: { // single/dual
        if (rest.isEmpty()) {
            QByteArray v(1, (char)scopeSingleDual);
            echoSimple(v);
        } else {
            scopeSingleDual = (quint8)rest[0];
            emit replyFrame(ack(true));
        }
        break;
    }
    case 0x14: { // mode (VFO + 1 byte)
        if (rest.size() == 1) {
            // read with VFO only
            quint8 vfo = (quint8)rest[0];
            QByteArray v(1, (char)scopeMode);
            echoVfo(vfo, v);
        } else if (rest.size() >= 2) {
            scopeMode = (quint8)rest[1];
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x15: { // span (VFO + 5 BCD freq)
        if (rest.size() == 1) {
            quint8 vfo = (quint8)rest[0];
            echoVfo(vfo, freqToBcd5(spanHzForIndex(scopeSpanIdx)));
        } else if (rest.size() >= 6) {
            quint64 f = bcd5ToFreq(rest.mid(1, 5));
            // Snap to nearest configured span so future reads round-trip.
            quint8 best = scopeSpanIdx;
            quint64 bestErr = UINT64_MAX;
            for (quint8 i = 0; i < 8; ++i) {
                quint64 err = (kIcom7300Spans[i] >= f) ?
                    (kIcom7300Spans[i] - f) : (f - kIcom7300Spans[i]);
                if (err < bestErr) { bestErr = err; best = i; }
            }
            scopeSpanIdx = best;
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x16: { // edge (VFO + 1 BCD byte)
        if (rest.size() == 1) {
            quint8 vfo = (quint8)rest[0];
            QByteArray v(1, (char)scopeEdge);
            echoVfo(vfo, v);
        } else if (rest.size() >= 2) {
            scopeEdge = (quint8)rest[1];
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x17: { // hold (VFO + 1 byte)
        if (rest.size() == 1) {
            quint8 vfo = (quint8)rest[0];
            QByteArray v(1, (char)(scopeHold ? 0x01 : 0x00));
            echoVfo(vfo, v);
        } else if (rest.size() >= 2) {
            scopeHold = ((quint8)rest[1] != 0);
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x19: { // reference (VFO + BCD-hi + BCD-lo + sign byte)
        if (rest.size() == 1) {
            quint8 vfo = (quint8)rest[0];
            // Ref is in 0.1 dB units, range ±300 → BCD as 4-digit positive value.
            quint16 mag = (scopeRefTenths >= 0) ? scopeRefTenths : -scopeRefTenths;
            QByteArray bcd = u16ToBcd2(mag);
            QByteArray v;
            v.append(bcd);
            v.append((char)(scopeRefTenths < 0 ? 0x01 : 0x00));
            echoVfo(vfo, v);
        } else if (rest.size() >= 4) {
            quint16 mag = bcd2ToU16(rest.mid(1, 2));
            quint8 negative = (quint8)rest[3];
            scopeRefTenths = (qint16)mag * (negative ? -1 : 1);
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x1A: { // speed (VFO + 1 byte)
        if (rest.size() == 1) {
            quint8 vfo = (quint8)rest[0];
            QByteArray v(1, (char)scopeSpeed);
            echoVfo(vfo, v);
        } else if (rest.size() >= 2) {
            scopeSpeed = (quint8)rest[1];
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x1B: { // during TX (no VFO byte)
        if (rest.isEmpty()) {
            QByteArray v(1, (char)scopeDuringTX);
            echoSimple(v);
        } else {
            scopeDuringTX = (quint8)rest[0];
            emit replyFrame(ack(true));
        }
        break;
    }
    case 0x1C: { // center type (VFO + 1 byte)
        if (rest.size() == 1) {
            quint8 vfo = (quint8)rest[0];
            QByteArray v(1, (char)scopeCenterType);
            echoVfo(vfo, v);
        } else if (rest.size() >= 2) {
            scopeCenterType = (quint8)rest[1];
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x1D: { // VBW (VFO + 1 byte)
        if (rest.size() == 1) {
            quint8 vfo = (quint8)rest[0];
            QByteArray v(1, (char)scopeVBW);
            echoVfo(vfo, v);
        } else if (rest.size() >= 2) {
            scopeVBW = (quint8)rest[1];
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    case 0x1F: { // RBW (VFO + 1 byte)
        if (rest.size() == 1) {
            quint8 vfo = (quint8)rest[0];
            QByteArray v(1, (char)scopeRBW);
            echoVfo(vfo, v);
        } else if (rest.size() >= 2) {
            scopeRBW = (quint8)rest[1];
            emit replyFrame(ack(true));
        } else {
            emit replyFrame(ack(false));
        }
        break;
    }
    default:
        // 0x1E (fixed edge freq) and any other sub-cmds: not implemented.
        // ACK silently — cluttering the log with these adds no signal.
        emit replyFrame(ack(true));
        break;
    }
}

void civEmulator::emitScopeWaveData()
{
    // Build a 475-pixel waveform (matches IC-7300 SpectrumLenMax). Pixel scale
    // is 0..160 (SpectrumAmpMax). Strategy:
    //   - Noise floor: log-mapped from the rig's mixer noise RMS.
    //   - Activity:    Gaussian-shaped peak at the center pixel scaled by the
    //                  current RX audio peak. So when something is being
    //                  received, the waterfall shows a centered "blob"; when
    //                  nothing is, only the noise floor wiggles.
    // This is intentionally synthetic — virtualrig has demodulated audio, not
    // an RF spectrum, but a centered peak is enough to exercise wfweb's
    // entire waterfall pipeline (parser, JSON, canvas paint).

    constexpr int kPixels = 475;
    constexpr int kAmpMax = 160;

    QByteArray pixels(kPixels, '\0');

    // Floor: 0 (silent) up to ~30 (noisy). Q15-ish noise RMS ≤ ~5000 in practice.
    int floor = 0;
    if (noiseRms > 0.0f) {
        // log-map: noiseRms 100 → ~10, 1000 → ~22, 5000 → ~32
        float v = 6.0f * std::log10(1.0f + noiseRms);
        if (v < 0) v = 0;
        if (v > 30) v = 30;
        floor = (int)v;
    }

    // Activity: map peak audio (0..32767) to 0..120 added on top of floor.
    int signalPk = 0;
    if (lastRxPeak > 0) {
        double db = 20.0 * std::log10((double)lastRxPeak / 32767.0);
        if (db < -60.0) db = -60.0;
        int v = (int)((db + 60.0) / 60.0 * 120.0);
        if (v < 0) v = 0;
        if (v > 120) v = 120;
        signalPk = v;
    }

    auto* rng = QRandomGenerator::global();
    const double sigma = (mode == 0x03 || mode == 0x07) ? 4.0  // CW: narrow
                       : (mode == 0x05) ? 30.0                  // FM: wide
                       : 12.0;                                  // SSB/AM/data
    const double center = (kPixels - 1) / 2.0;
    const double twoSigma2 = 2.0 * sigma * sigma;
    for (int i = 0; i < kPixels; ++i) {
        double dx = i - center;
        double bell = std::exp(-(dx * dx) / twoSigma2);
        int v = floor + (int)((double)signalPk * bell)
              + (int)(rng->bounded(5)) - 2;
        if (v < 0) v = 0;
        if (v > kAmpMax) v = kAmpMax;
        pixels[i] = (char)v;
    }

    // Frame layout (per Icom ICD, after `27 00 [recv]`):
    //   seq=1:    BCD seq, BCD seqMax, mode, lower-freq(5), upper-freq(5), oor
    //   seq=2..N-1: BCD seq, BCD seqMax, 50 pixel bytes
    //   seq=N:     BCD seq, BCD seqMax, remaining pixel bytes
    // For IC-7300: N=11, divisions 2..10 carry 50 px each, division 11 carries 25.
    const quint8 seqMax = 11;
    auto seqByte = [](quint8 n) -> char {
        return (char)(((n / 10) << 4) | (n % 10));
    };
    auto sendDivision = [&](quint8 seq, const QByteArray& payload) {
        QByteArray pl;
        pl.append((char)0x27);
        pl.append((char)0x00);
        pl.append((char)0x00);     // receiver: main
        pl.append(seqByte(seq));
        pl.append(seqByte(seqMax));
        pl.append(payload);
        emit replyFrame(buildFrame(ctlCiv, pl));
    };

    // Header (sequence 1). Decide edges from current mode/span/freq.
    quint64 halfSpan = spanHzForIndex(scopeSpanIdx);
    QByteArray header;
    header.append((char)scopeMode);
    if (scopeMode == 0) {
        // Center mode: rig sends [center, half-bandwidth].
        header.append(freqToBcd5(freq));
        header.append(freqToBcd5(halfSpan));
    } else {
        // Fixed/scroll: rig sends [lower, upper].
        quint64 lower = (freq > halfSpan) ? (freq - halfSpan) : 0ULL;
        quint64 upper = freq + halfSpan;
        header.append(freqToBcd5(lower));
        header.append(freqToBcd5(upper));
    }
    header.append((char)0x00); // in-range
    sendDivision(1, header);

    // Pixel divisions 2..10 (50 px each = 450 px), then 11 (25 px) = 475 px total.
    int offset = 0;
    for (quint8 s = 2; s <= 10; ++s) {
        sendDivision(s, pixels.mid(offset, 50));
        offset += 50;
    }
    sendDivision(11, pixels.mid(offset, kPixels - offset));
}
