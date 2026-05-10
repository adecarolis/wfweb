#ifndef EXTAUDIO_H
#define EXTAUDIO_H

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

// PulseAudio bridge for one external slot. Owns three modules so that an
// external program can plug in via the system's audio panel and see clean,
// well-named devices in BOTH directions:
//
//     virtualrig-X-output  (sink)    JS8Call's Output  — TX from your program
//     virtualrig-X-input   (source)  JS8Call's Input   — RX coming from the rig
//
// Apps typically hide ".monitor" sources from their input dropdowns, so we
// can't just expose the monitor of a null-sink for the RX direction —
// JS8Call wouldn't see it. The trick is module-remap-source: virtualrig
// writes RX audio into an internal null-sink (-rxbus), and a remap-source
// re-exposes that sink's monitor as a regular-named source.
//
// Internal layout (visible in pavucontrol but not user-facing):
//     virtualrig-X-rxbus   (sink)    virtualrig writes mixer-RX here
//     virtualrig-X-rxbus.monitor → remapped to virtualrig-X-input
//     virtualrig-X-output.monitor   virtualrig reads user TX from here
//
// pactl is used for module load/unload (one-shot lifecycle); streaming uses
// pa_simple with explicit small buffers so the monitor stays in lockstep.
class ExtAudio : public QObject
{
    Q_OBJECT
public:
    ExtAudio(const QString& rxBusSinkName,    // internal sink, virtualrig writes
             const QString& outputSinkName,   // user-facing sink (Output)
             const QString& inputSourceName,  // user-facing source (Input)
             const QString& slotLabel,
             QObject* parent = nullptr);
    ~ExtAudio() override;

    // start(): load null-sinks (clean any of ours left over from a crashed
    // run first), spawn rx/tx threads, open pa_simple streams.
    void start();

    // stop(): signal threads, close streams, unload our modules. Idempotent.
    void stop();

    // Main-thread → RX worker. The worker drains this buffer at 48 kHz
    // (paced by pa_simple_write) and pads with silence when empty so the
    // sink stays alive without producing a stuttery "RX present / RX absent"
    // pattern that breaks AGC and noise-blanker logic in JS8Call etc.
    void queueRxAudio(const QByteArray& pcm);

signals:
    // Emitted from the TX worker thread (queued cross-thread connection
    // expected). One 20 ms chunk @ 48 kHz int16 LE mono.
    void txChunkReady(const QByteArray& pcm);

private:
    void rxLoop();
    void txLoop();
    void unloadOurModules();
    // userFacing=false hides the sink from JS8Call's Output dropdown via
    // media.class=Audio/Sink/Internal — used for the internal RX bus.
    int  loadNullSink(const QString& sinkName, const QString& description,
                      bool userFacing);
    int  loadRemapSource(const QString& sourceName, const QString& masterName,
                         const QString& description);

    QString rxBusSink_;     // "virtualrig-X-rxbus"          (internal)
    QString outputSink_;    // "virtualrig-X-output"         (user-facing)
    QString inputSource_;   // "virtualrig-X-input"          (user-facing)
    QString outputMonitor_; // "virtualrig-X-output.monitor" (we record from)
    QString slotLabel_;

    int moduleRxBus_  = -1; // pactl module indices, -1 if not loaded
    int moduleOutput_ = -1;
    int moduleInput_  = -1;

    std::atomic<bool> running_{false};
    QThread* rxThread_ = nullptr;
    QThread* txThread_ = nullptr;

    // RX queue: main thread appends, RX worker drains.
    QMutex rxMx_;
    QWaitCondition rxCv_;
    QByteArray rxBuf_;
};

#endif // EXTAUDIO_H
