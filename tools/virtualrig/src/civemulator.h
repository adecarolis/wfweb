#ifndef CIVEMULATOR_H
#define CIVEMULATOR_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>

// Minimal CI-V responder. Parses inbound frames from a client (as received
// by icomServer and forwarded via haveDataFromServer), tracks per-rig state,
// emits reply frames via replyFrame(), and emits unsolicited transceive
// broadcasts when state changes so the client UI tracks our virtual rig.
class civEmulator : public QObject
{
    Q_OBJECT

public:
    // radioCiv  — CI-V address we answer as (e.g. 0x98 for IC-7610).
    explicit civEmulator(quint8 radioCiv, QObject* parent = nullptr);

    void setName(const QString& n) { name = n; }

    bool isTransmitting() const { return ptt; }
    quint64 frequency() const { return freq; }
    quint8 rigMode() const { return mode; }

    // Drive the synthesized S-meter from an RX audio peak (0..32767).
    void setSMeterFromPeak(quint16 peak);
    // Drive synthesized TX meters (Po/ALC) from a mic audio peak (0..32767).
    void setTxMeterFromPeak(quint16 peak);
    // Drive the synthesized waterfall noise floor from the rig's mixer noise
    // RMS (in Q15 units, same scale the mixer reports).
    void setNoiseFloorFromRms(float rms) { noiseRms = rms; }

public slots:
    // Full CI-V frame as received from the client, including 0xFE 0xFE
    // preamble and 0xFD trailer.
    void onCivFromClient(const QByteArray& frame);

signals:
    // Full CI-V frame to send back to the client, ready for
    // icomServer::dataForServer().
    void replyFrame(QByteArray frame);

    // Emitted on PTT edges so the virtualRig can gate mixer input.
    void pttChanged(bool on);

private:
    // State.
    QString name;
    quint8 radioCiv;
    quint8 ctlCiv = 0xE0; // last-seen controller CIV (updated on each inbound)
    quint64 freq = 14074000ULL; // 20 m FT8 default
    quint8 mode = 0x01;         // USB
    quint8 filter = 0x01;       // FIL1
    quint8 dataMode = 0x00;     // no DATA
    bool ptt = false;
    quint16 gains[16] = {0};    // indexed by 0x14 subcommand (full byte)
    quint16 sMeter = 0;         // 0 = silent (S0). Updated later from mixer audio peak.
    quint16 poMeter = 0;        // 0x15 0x11 Power Meter (0..255 BCD).
    quint16 swrMeter = 0;       // 0x15 0x12 SWR Meter.
    quint16 alcMeter = 0;       // 0x15 0x13 ALC Meter.

    // Scope (0x27) state. Default values mirror what an IC-7300 boots with so
    // wfweb sees a sane rig the moment it queries us.
    bool scopeOn = true;             // 0x10
    bool scopeDataOutput = false;    // 0x11
    quint8 scopeSingleDual = 0;      // 0x13 (single-scope rig, always 0)
    quint8 scopeMode = 0;            // 0x14 (0=center, 1=fixed, 2=scroll-C, 3=scroll-F)
    quint8 scopeSpanIdx = 1;         // 0x15 (index into IC-7300 spans table; 1 = ±5 kHz)
    quint8 scopeEdge = 1;            // 0x16 (1..3 in BCD)
    bool scopeHold = false;          // 0x17
    qint16 scopeRefTenths = 0;       // 0x19 (-300..+300, in 0.1 dB units)
    quint8 scopeSpeed = 1;           // 0x1a (0=fast, 1=mid, 2=slow)
    quint8 scopeDuringTX = 0;        // 0x1b
    quint8 scopeCenterType = 0;      // 0x1c
    quint8 scopeVBW = 0;             // 0x1d (narrow/wide)
    quint8 scopeRBW = 0;             // 0x1f (auto/fine/coarse)

    // Latest RX audio peak (0..32767) used to shape the synthesized waterfall.
    quint16 lastRxPeak = 0;
    float noiseRms = 0.0f;          // mixer noise RMS, drives the noise floor.

    // Periodic spectrum-frame emitter. Real IC-7300 pushes ~30 fps; we use a
    // 50 ms tick (20 fps) — enough to make the waterfall scroll smoothly while
    // keeping CPU/UDP cost trivial.
    QTimer* scopeTimer = nullptr;

    // Helpers.
    QByteArray buildFrame(quint8 to, const QByteArray& payload) const;
    QByteArray ack(bool ok = true) const;
    QByteArray freqToBcd5(quint64 hz) const;
    quint64 bcd5ToFreq(const QByteArray& bcd) const;
    QByteArray u16ToBcd2(quint16 v) const;
    quint16 bcd2ToU16(const QByteArray& b) const;

    // Emit a broadcast (to=0x00) transceive update for a cmd/payload.
    void emitTransceive(const QByteArray& payload);

    // 0x27 scope handlers.
    void handleScopeCommand(const QByteArray& body);
    quint64 spanHzForIndex(quint8 idx) const;   // half-bandwidth in Hz
    void emitScopeWaveData();
    void updateScopeTimerState();

    // Bottom 4 bytes BCD of an unsigned int (1 Hz..10 MHz). Matches Icom's
    // little-endian BCD layout used for span/ref payloads.
    QByteArray uintToBcd5(quint64 v) const { return freqToBcd5(v); }
};

#endif
