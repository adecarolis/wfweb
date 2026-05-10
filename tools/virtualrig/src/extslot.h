#ifndef EXTSLOT_H
#define EXTSLOT_H

#include <QObject>
#include <QByteArray>

#include "rigslot.h"
#include "audioconverter.h"

class channelMixer;
class ExtRigctld;
class ExtAudio;

// External-program participant in the rig ring. Looks like a regular RigSlot
// to the mixer (freq/mode/PTT, RX audio in, TX audio out), but its CAT comes
// from a Hamlib NET rigctl TCP server and its audio comes from a PulseAudio
// sink+source pair instead of an Icom LAN UDP triplet. JS8Call (or any other
// outside program — wsjt-x, fldigi, etc.) connects via:
//   - Rig:    Hamlib NET rigctl  →  127.0.0.1:<rigctldPort>
//   - Output: virtualrig-X-output  (sink, the program writes its TX here)
//   - Input:  virtualrig-X-input   (source, the program reads its RX here)
class ExtSlot : public RigSlot
{
    Q_OBJECT
public:
    struct Config {
        int index = 0;
        QString name = "external";
        quint16 rigctldPort = 4532;     // Hamlib NET rigctl (rigctld) port
        QString rxBusSink;               // virtualrig-X-rxbus  (internal sink)
        QString outputSink;              // virtualrig-X-output (user's Output)
        QString inputSource;             // virtualrig-X-input  (user's Input)
        quint64 initialFreq = 14078000;  // JS8 USB calling
        quint8  initialMode = 0x01;      // USB
        quint32 initialWidth = 3000;
    };

    ExtSlot(const Config& cfg, channelMixer* mixer, QObject* parent = nullptr);
    ~ExtSlot() override;

    void start() override;
    void stop() override;

    quint64 freq() const override { return freq_; }
    quint8  mode() const override { return mode_; }
    bool    isTransmitting() const override { return ptt_; }

    quint16 rigctldPort()   const override { return cfg.rigctldPort; }
    QString paOutputSink()  const override { return cfg.outputSink; }
    QString paInputSource() const override { return cfg.inputSource; }

    // CAT-side mutators — called from ExtRigctld on the main thread.
    void setFreq(quint64 hz);
    void setMode(quint8 icomMode, quint32 widthHz);
    void setPtt(bool on);
    void setSplit(bool enabled, quint64 splitTxFreq);
    void setSplitMode(quint8 icomMode, quint32 widthHz);

    quint32 modeWidth()       const { return modeWidth_; }
    bool    splitEnabled()    const { return splitEnabled_; }
    quint64 splitTxFreq()     const { return splitTxFreq_; }
    quint8  splitMode()       const { return splitMode_; }
    quint32 splitModeWidth()  const { return splitModeWidth_; }

    // Hamlib mode-name ↔ Icom mode-code mapping. Public so tests / callers
    // can use it without going through a slot instance.
    static quint8  modeFromName(const QString& name);
    static QString modeToName(quint8 icomMode);

public slots:
    void onRxAudioFromMixer(int dstRig, const audioPacket& pkt) override;

private slots:
    void onPaTxChunk(const QByteArray& pcm); // from ExtAudio (queued, main thread)

private:
    Config cfg;
    channelMixer* mixer;
    ExtRigctld* rigctld_ = nullptr;
    ExtAudio*   audio_   = nullptr;

    quint64 freq_ = 14078000;
    quint8  mode_ = 0x01;
    quint32 modeWidth_ = 3000;
    bool    ptt_ = false;
    bool    splitEnabled_ = false;
    quint64 splitTxFreq_ = 14078000;
    quint8  splitMode_ = 0x01;
    quint32 splitModeWidth_ = 3000;

    quint32 rxSeq_ = 0;
};

#endif // EXTSLOT_H
