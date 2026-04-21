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
};

#endif
