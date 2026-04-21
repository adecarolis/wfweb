#ifndef VIRTUALRIG_H
#define VIRTUALRIG_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QString>
#include <QByteArray>
#include <QMutex>

#include "rigserver.h"
#include "rigidentities.h"
#include "audioconverter.h"

class icomServer;
class civEmulator;
class channelMixer;

// One virtual rig: owns an icomServer (in its own thread), a civEmulator,
// and talks to a shared channelMixer. The wfweb client connects to it as
// if it were a real Icom over LAN.
class virtualRig : public QObject
{
    Q_OBJECT

public:
    struct Config {
        int index = 0;                 // rig slot in the mixer
        QString name = "virtual-rig";
        quint8 civAddr = 0x94;         // IC-7300 — HasCommand29=false, keeps client protocol simple
        quint16 controlPort = 50001;
        quint16 civPort = 50002;
        quint16 audioPort = 50003;
    };

    virtualRig(const Config& cfg, channelMixer* mixer, QObject* parent = nullptr);
    ~virtualRig();

    void start();
    void stop();

    const Config& config() const { return cfg; }

    // State accessors used by the mixer for freq/mode-aware routing. Safe to
    // read from the main thread (civEmulator mutates there via queued slots).
    quint64 freq() const;
    quint8 mode() const;

private slots:
    void onTxAudioFromClient(const audioPacket& pkt);
    void onRxAudioFromMixer(int dstRig, const audioPacket& pkt);
    void onPttChanged(bool on);
    void emitIdleRx();

private:
    Config cfg;
    channelMixer* mixer;

    SERVERCONFIG serverCfg;
    RIGCONFIG rigCfg;
    rigCapabilities caps;

    icomServer* server = nullptr;
    QThread* serverThread = nullptr;
    civEmulator* civ = nullptr;
    QTimer* idleRxTimer = nullptr;

    bool ptt = false;
    quint32 rxSeq = 0;

    // Mixing buffer — real audio from the mixer is appended here, and the
    // 20 ms idle timer drains exactly one 20 ms chunk per tick (padding with
    // silence if the buffer is short). This gives the client a single cadence
    // and packet size so its jitter buffer never sees interleaved silence.
    QMutex rxMutex;
    QByteArray rxBuffer;

    // 2nd-order Butterworth low-pass, re-tuned when the rig's mode changes.
    // Applied on TX audio so the bus only carries what this rig would
    // physically emit: SSB ~3.5 kHz, FM ~12 kHz, CW/RTTY ~500 Hz, etc.
    struct BiquadLpf {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        quint8 tunedForMode = 0xFF;
        void tune(float sampleRate, float cutoffHz);
        float process(float x);
    };
    BiquadLpf txLpf;
};

#endif
