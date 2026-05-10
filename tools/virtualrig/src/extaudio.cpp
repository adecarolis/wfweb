#include "extaudio.h"

#include <QDebug>
#include <QProcess>
#include <QStringList>
#include <QMutexLocker>
#include <QByteArray>
#include <QString>
#include <QThread>

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/sample.h>

namespace {

// 20 ms @ 48 kHz int16 mono = 960 samples = 1920 bytes — the cadence the
// rest of the bus uses (see virtualrig.cpp emitIdleRx).
constexpr int kSampleRate = 48000;
constexpr int kChunkBytes = 48 * 20 * 2;

// Run pactl synchronously. Returns trimmed stdout on success, empty QByteArray
// on failure. argc/argv split as a QStringList.
QByteArray runPactl(const QStringList& args, int* exitCode = nullptr)
{
    QProcess p;
    p.start("pactl", args);
    if (!p.waitForStarted(2000)) {
        qWarning() << "pactl: failed to start";
        if (exitCode) *exitCode = -1;
        return QByteArray();
    }
    p.waitForFinished(5000);
    if (exitCode) *exitCode = p.exitCode();
    return p.readAllStandardOutput().trimmed();
}

// Worker thread that just calls a function. Avoids subclassing QThread.
class FuncThread : public QThread {
public:
    FuncThread(std::function<void()> fn, QObject* parent = nullptr)
        : QThread(parent), fn_(std::move(fn)) {}
    void run() override { fn_(); }
private:
    std::function<void()> fn_;
};

} // namespace

ExtAudio::ExtAudio(const QString& rxBusSinkName,
                   const QString& outputSinkName,
                   const QString& inputSourceName,
                   const QString& slotLabel,
                   QObject* parent)
    : QObject(parent),
      rxBusSink_(rxBusSinkName),
      outputSink_(outputSinkName),
      inputSource_(inputSourceName),
      outputMonitor_(outputSinkName + ".monitor"),
      slotLabel_(slotLabel)
{
}

ExtAudio::~ExtAudio()
{
    stop();
}

int ExtAudio::loadNullSink(const QString& sinkName, const QString& description,
                           bool userFacing)
{
    // pactl load-module module-null-sink sink_name=NAME sink_properties=...
    //
    // Two parser quirks of PipeWire's PA emulation we work around here:
    //   1. sink_properties VALUES get split on ASCII whitespace regardless of
    //      any quoting (pa_proplist_from_string isn't fully honored), so a
    //      space inside a description truncates it to its first word.
    //      Substitute NBSP (U+00A0) — same visual rendering in any UI, but
    //      the parser treats it as a non-space character.
    //   2. Multiple properties ARE separated by a real space at the outer
    //      level, so we can combine description + class hints.
    //
    // device.class=filter + node.passive=true tell wireplumber this isn't a
    // regular playback device and shouldn't be auto-promoted to default or
    // auto-targeted by user streams. Without this, creating new null-sinks
    // can divert JS8Call's TX audio (and even system audio) onto them,
    // manifesting as silence on the real speakers and stray noise on
    // whatever path wireplumber loops things through.
    //
    // For the *internal* RX bus we go further with media.class=Audio/Sink/
    // Internal: that hides the node from app dropdowns entirely so the user
    // can't accidentally pick it as JS8Call's Output. The user-facing output
    // sink leaves media.class at its default Audio/Sink so JS8Call still
    // sees it.
    QString safeDesc = description;
    safeDesc.replace(' ', QChar(0x00A0));
    // device.class=filter discourages wireplumber auto-promotion to default
    // sink, but we deliberately do NOT set node.passive=true — PipeWire's
    // passive nodes don't pull audio through to their monitor when no
    // downstream node is attached, which silently breaks the RX path.
    QString props = "device.description=" + safeDesc
                  + " device.class=filter";
    Q_UNUSED(userFacing)
    // (Tried media.class=Audio/Sink/Internal to hide the rxbus from app
    // dropdowns — it works for hiding, but PipeWire then doesn't generate
    // a usable monitor for it, breaking the remap-source. So we just leave
    // the rxbus visible with a description that says "internal — don't
    // pick" and rely on the user to read it.)
    QStringList args;
    args << "load-module" << "module-null-sink"
         << ("sink_name=" + sinkName)
         << ("sink_properties=" + props);
    int rc = 0;
    QByteArray out = runPactl(args, &rc);
    if (rc != 0) {
        qWarning() << "ExtAudio" << slotLabel_ << "load-module failed for" << sinkName
                   << "rc=" << rc;
        return -1;
    }
    bool ok = false;
    int idx = QString::fromUtf8(out).toInt(&ok);
    if (!ok) {
        qWarning() << "ExtAudio" << slotLabel_ << "could not parse module index from:"
                   << out;
        return -1;
    }
    return idx;
}

int ExtAudio::loadRemapSource(const QString& sourceName, const QString& masterName,
                              const QString& description)
{
    // pactl load-module module-remap-source master=MASTER source_name=NAME source_properties=...
    // The remap re-exposes a monitor source under a regular-looking name so
    // apps that filter out ".monitor" sources (JS8Call, wsjt-x, fldigi, ...)
    // still see a usable Input device.
    //
    // device.class=filter discourages wireplumber from auto-promoting this
    // source as default-input. Don't use node.passive=true here either —
    // it stops audio from flowing through (same caveat as for the sinks).
    // NBSP substitution: see loadNullSink.
    QString safeDesc = description;
    safeDesc.replace(' ', QChar(0x00A0));
    QString props = "device.description=" + safeDesc
                  + " device.class=filter";
    QStringList args;
    args << "load-module" << "module-remap-source"
         << ("master=" + masterName)
         << ("source_name=" + sourceName)
         << ("source_properties=" + props);
    int rc = 0;
    QByteArray out = runPactl(args, &rc);
    if (rc != 0) {
        qWarning() << "ExtAudio" << slotLabel_ << "load-module remap failed for"
                   << sourceName << "rc=" << rc;
        return -1;
    }
    bool ok = false;
    int idx = QString::fromUtf8(out).toInt(&ok);
    if (!ok) {
        qWarning() << "ExtAudio" << slotLabel_
                   << "could not parse module index from:" << out;
        return -1;
    }
    return idx;
}

void ExtAudio::unloadOurModules()
{
    // Find any of our null-sinks or remap-sources by name and unload them.
    // Survives crashes that left modules behind.
    int rc = 0;
    QByteArray out = runPactl({"list", "short", "modules"}, &rc);
    if (rc != 0) return;
    const QList<QByteArray> lines = out.split('\n');
    const QByteArray sinkRxBusKey  = ("sink_name="   + rxBusSink_).toUtf8();
    const QByteArray sinkOutputKey = ("sink_name="   + outputSink_).toUtf8();
    const QByteArray sourceInKey   = ("source_name=" + inputSource_).toUtf8();
    for (const QByteArray& line : lines) {
        if (line.isEmpty()) continue;
        QList<QByteArray> cols = line.split('\t');
        if (cols.size() < 2) continue;
        const QByteArray rest = (cols.size() >= 3) ? cols[2] : QByteArray();
        const bool match =
            (cols[1] == "module-null-sink"   && (rest.contains(sinkRxBusKey)  ||
                                                 rest.contains(sinkOutputKey))) ||
            (cols[1] == "module-remap-source" && rest.contains(sourceInKey));
        if (!match) continue;
        bool ok = false;
        int idx = QString::fromUtf8(cols[0]).toInt(&ok);
        if (!ok) continue;
        runPactl({"unload-module", QString::number(idx)});
    }
}

void ExtAudio::start()
{
    if (running_) return;

    // Tidy up if a previous run died with our modules still loaded.
    unloadOurModules();

    // Order matters: rxBus must exist before we can remap its monitor.
    // Descriptions chosen so they sort/display unambiguously in pavucontrol
    // and in JS8Call's audio dropdowns. The internal -rxbus sink unavoidably
    // shows up too, but its description tells the user not to pick it.
    moduleRxBus_  = loadNullSink(rxBusSink_,
                                 slotLabel_ + " RX bus -- DO NOT SELECT",
                                 /*userFacing*/ false);
    moduleOutput_ = loadNullSink(outputSink_,
                                 slotLabel_ + " Output (TX from your program)",
                                 /*userFacing*/ true);
    moduleInput_  = loadRemapSource(inputSource_, rxBusSink_ + ".monitor",
                                    slotLabel_ + " Input (RX from the rig)");
    if (moduleRxBus_ < 0 || moduleOutput_ < 0 || moduleInput_ < 0) {
        qWarning() << "ExtAudio" << slotLabel_
                   << "module load failed — is a PulseAudio/PipeWire server running?";
        // Continue anyway so the rigctld side still works; just no audio.
    }

    running_ = true;

    rxThread_ = new FuncThread([this]() { rxLoop(); }, this);
    rxThread_->setObjectName("extaudio-rx-" + slotLabel_);
    rxThread_->start();

    txThread_ = new FuncThread([this]() { txLoop(); }, this);
    txThread_->setObjectName("extaudio-tx-" + slotLabel_);
    txThread_->start();
}

void ExtAudio::stop()
{
    if (!running_.exchange(false)) {
        // Already stopped — but still try to unload modules in case start()
        // partially completed (pa_simple failed but module loaded).
        if (moduleRxBus_ >= 0 || moduleOutput_ >= 0 || moduleInput_ >= 0) {
            unloadOurModules();
            moduleRxBus_ = moduleOutput_ = moduleInput_ = -1;
        }
        return;
    }

    // Wake the RX thread so it can notice running_=false.
    {
        QMutexLocker lock(&rxMx_);
        rxCv_.wakeAll();
    }

    if (txThread_) {
        txThread_->wait(2000);
        delete txThread_;
        txThread_ = nullptr;
    }
    if (rxThread_) {
        rxThread_->wait(2000);
        delete rxThread_;
        rxThread_ = nullptr;
    }

    // Unload in reverse load order: the remap-source depends on rxbus's
    // monitor, so kill it first before tearing rxbus down.
    if (moduleInput_ >= 0) {
        runPactl({"unload-module", QString::number(moduleInput_)});
        moduleInput_ = -1;
    }
    if (moduleOutput_ >= 0) {
        runPactl({"unload-module", QString::number(moduleOutput_)});
        moduleOutput_ = -1;
    }
    if (moduleRxBus_ >= 0) {
        runPactl({"unload-module", QString::number(moduleRxBus_)});
        moduleRxBus_ = -1;
    }
}

void ExtAudio::queueRxAudio(const QByteArray& pcm)
{
    QMutexLocker lock(&rxMx_);
    rxBuf_.append(pcm);
    // Cap runaway growth — same 500 ms ceiling internal slots use.
    const int maxBytes = 48 * 500 * 2;
    if (rxBuf_.size() > maxBytes) {
        rxBuf_.remove(0, rxBuf_.size() - maxBytes);
    }
    rxCv_.wakeAll();
}

void ExtAudio::rxLoop()
{
    pa_sample_spec spec;
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = kSampleRate;
    spec.channels = 1;

    // Tight buffer (~40 ms target, ~100 ms hard cap) so rxBuf_ → sink lag stays
    // small and PipeWire doesn't quietly aggregate our writes into giant chunks
    // that miss the monitor's sample window. (uint32_t)-1 means "let server pick".
    pa_buffer_attr ba;
    ba.maxlength = (uint32_t)-1;
    ba.tlength   = kChunkBytes * 2;
    ba.prebuf    = (uint32_t)-1;
    ba.minreq    = (uint32_t)-1;
    ba.fragsize  = (uint32_t)-1;

    int err = 0;
    pa_simple* s = pa_simple_new(
        nullptr,                                    // default server
        ("virtualrig-" + slotLabel_).toUtf8().constData(),
        PA_STREAM_PLAYBACK,
        rxBusSink_.toUtf8().constData(),            // play to internal rxbus sink
        "rx",
        &spec,
        nullptr,
        &ba,
        &err);
    if (!s) {
        qWarning() << "ExtAudio" << slotLabel_ << "pa_simple_new(playback) failed:"
                   << pa_strerror(err);
        return;
    }

    // Tight loop; pa_simple_write blocks when the playback buffer is full,
    // so it self-paces at 48 kHz. We pad with silence whenever the queue is
    // dry, keeping the sink active so the consumer's record stream doesn't
    // see gaps.
    QByteArray silence(kChunkBytes, '\0');
    while (running_) {
        QByteArray chunk;
        {
            QMutexLocker lock(&rxMx_);
            if (rxBuf_.isEmpty()) {
                chunk = silence;
            } else {
                int take = qMin(rxBuf_.size(), kChunkBytes);
                chunk = rxBuf_.left(take);
                rxBuf_.remove(0, take);
                if (chunk.size() < kChunkBytes) {
                    chunk.append(QByteArray(kChunkBytes - chunk.size(), '\0'));
                }
            }
        }
        if (pa_simple_write(s, chunk.constData(), chunk.size(), &err) < 0) {
            qWarning() << "ExtAudio" << slotLabel_ << "pa_simple_write failed:"
                       << pa_strerror(err);
            break;
        }
    }
    pa_simple_drain(s, &err);
    pa_simple_free(s);
}

void ExtAudio::txLoop()
{
    pa_sample_spec spec;
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = kSampleRate;
    spec.channels = 1;

    pa_buffer_attr ba;
    ba.maxlength = (uint32_t)-1;
    ba.tlength   = (uint32_t)-1;
    ba.prebuf    = (uint32_t)-1;
    ba.minreq    = (uint32_t)-1;
    ba.fragsize  = kChunkBytes;                     // record one 20 ms frag at a time

    int err = 0;
    pa_simple* s = pa_simple_new(
        nullptr,
        ("virtualrig-" + slotLabel_).toUtf8().constData(),
        PA_STREAM_RECORD,
        outputMonitor_.toUtf8().constData(),        // record from output.monitor
        "tx",
        &spec,
        nullptr,
        &ba,
        &err);
    if (!s) {
        qWarning() << "ExtAudio" << slotLabel_ << "pa_simple_new(record) failed:"
                   << pa_strerror(err);
        return;
    }

    QByteArray buf;
    buf.resize(kChunkBytes);
    while (running_) {
        if (pa_simple_read(s, buf.data(), buf.size(), &err) < 0) {
            qWarning() << "ExtAudio" << slotLabel_ << "pa_simple_read failed:"
                       << pa_strerror(err);
            break;
        }
        emit txChunkReady(buf);
    }
    pa_simple_free(s);
}
