#ifndef RIGSLOT_H
#define RIGSLOT_H

#include <QObject>
#include <QString>

#include "audioconverter.h"  // audioPacket

// Common interface every mixer participant exposes. Two concrete kinds today:
//   - virtualRig: full Icom LAN UDP emulator; a wfweb client connects to it
//   - extSlot:    Hamlib NET rigctl + PulseAudio bridge; an outside program
//                 (e.g. JS8Call) connects to it
// The mixer is symmetric — it doesn't care which kind a slot is, only what
// frequency/mode/PTT each slot reports and where to deliver RX audio.
class RigSlot : public QObject
{
    Q_OBJECT
public:
    enum Kind { Internal, External };

    RigSlot(int idx, Kind kind, const QString& name, QObject* parent = nullptr)
        : QObject(parent), idx_(idx), kind_(kind), name_(name) {}
    ~RigSlot() override = default;

    int index() const { return idx_; }
    Kind kind() const { return kind_; }
    const QString& name() const { return name_; }

    virtual quint64 freq() const = 0;
    virtual quint8  mode() const = 0;
    virtual bool    isTransmitting() const = 0;

    virtual void start() = 0;
    virtual void stop()  = 0;

    // Connection details surfaced in the control panel. Internal slots leave
    // these empty / 0; external slots fill them so the user can see at a
    // glance where to point JS8Call.
    virtual quint16 rigctldPort()   const { return 0; }
    virtual QString paOutputSink()  const { return QString(); }  // user picks as Output (sink)
    virtual QString paInputSource() const { return QString(); }  // user picks as Input  (source)

public slots:
    // mixer dispatches one packet per destination; slot ignores anything not
    // addressed to it. Connect with a queued connection so it runs in this
    // slot's thread context.
    virtual void onRxAudioFromMixer(int dstRig, const audioPacket& pkt) = 0;

protected:
    int idx_;
    Kind kind_;
    QString name_;
};

#endif // RIGSLOT_H
