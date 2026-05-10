#include "extslot.h"
#include "channelmixer.h"
#include "extrigctld.h"
#include "extaudio.h"

#include <QDebug>
#include <QTime>

namespace {

// Hamlib mode-name table. Keep ordering deterministic so modeFromName picks
// the canonical Icom code we emit on get_mode round-trips.
struct ModeMap {
    const char* name;
    quint8 icom;
};
const ModeMap kModes[] = {
    { "USB",    0x01 },
    { "LSB",    0x00 },
    { "AM",     0x02 },
    { "CW",     0x03 },
    { "RTTY",   0x04 },
    { "FM",     0x05 },
    { "CWR",    0x07 },
    { "RTTYR",  0x08 },
    { "PKTUSB", 0x01 },     // DATA mode on USB — JS8Call typically reports this
    { "PKTLSB", 0x00 },     // DATA mode on LSB
    { "PKTFM",  0x05 },
    { "DV",     0x17 },
};

} // namespace

quint8 ExtSlot::modeFromName(const QString& name)
{
    QString upper = name.toUpper();
    for (const auto& m : kModes) {
        if (upper == QString::fromLatin1(m.name)) return m.icom;
    }
    return 0x01; // default USB
}

QString ExtSlot::modeToName(quint8 icomMode)
{
    switch (icomMode) {
    case 0x00: return "LSB";
    case 0x01: return "USB";
    case 0x02: return "AM";
    case 0x03: return "CW";
    case 0x04: return "RTTY";
    case 0x05: return "FM";
    case 0x07: return "CWR";
    case 0x08: return "RTTYR";
    case 0x17: return "DV";
    default:   return "USB";
    }
}

ExtSlot::ExtSlot(const Config& cfg, channelMixer* mixer, QObject* parent)
    : RigSlot(cfg.index, RigSlot::External, cfg.name, parent),
      cfg(cfg), mixer(mixer),
      freq_(cfg.initialFreq), mode_(cfg.initialMode), modeWidth_(cfg.initialWidth),
      splitTxFreq_(cfg.initialFreq), splitMode_(cfg.initialMode), splitModeWidth_(cfg.initialWidth)
{
}

ExtSlot::~ExtSlot()
{
    stop();
}

void ExtSlot::start()
{
    rigctld_ = new ExtRigctld(this, this);
    if (!rigctld_->listen(cfg.rigctldPort)) {
        qWarning() << "ExtSlot" << name() << "rigctld failed to listen on" << cfg.rigctldPort;
    }

    audio_ = new ExtAudio(cfg.rxBusSink, cfg.outputSink, cfg.inputSource, name(), this);
    QObject::connect(audio_, &ExtAudio::txChunkReady,
                     this, &ExtSlot::onPaTxChunk,
                     Qt::QueuedConnection);
    QObject::connect(mixer, &channelMixer::rxAudioForRig,
                     this, &ExtSlot::onRxAudioFromMixer,
                     Qt::QueuedConnection);
    audio_->start();

    qInfo().noquote() << QString("ExtSlot %1 up — rigctld :%2  output=%3  input=%4")
        .arg(name()).arg(cfg.rigctldPort).arg(cfg.outputSink).arg(cfg.inputSource);
}

void ExtSlot::stop()
{
    if (audio_) {
        audio_->stop();
        audio_->deleteLater();
        audio_ = nullptr;
    }
    if (rigctld_) {
        rigctld_->stop();
        rigctld_->deleteLater();
        rigctld_ = nullptr;
    }
}

void ExtSlot::setFreq(quint64 hz)
{
    freq_ = hz;
}

void ExtSlot::setMode(quint8 icomMode, quint32 widthHz)
{
    mode_ = icomMode;
    if (widthHz > 0) modeWidth_ = widthHz;
}

void ExtSlot::setPtt(bool on)
{
    if (ptt_ == on) return;
    ptt_ = on;
    qDebug() << "ExtSlot" << name() << "PTT" << (on ? "ON" : "OFF");
}

void ExtSlot::setSplit(bool enabled, quint64 splitTxFreq)
{
    splitEnabled_ = enabled;
    splitTxFreq_ = splitTxFreq;
}

void ExtSlot::setSplitMode(quint8 icomMode, quint32 widthHz)
{
    splitMode_ = icomMode;
    if (widthHz > 0) splitModeWidth_ = widthHz;
}

void ExtSlot::onRxAudioFromMixer(int dstRig, const audioPacket& pkt)
{
    if (dstRig != index()) return;
    if (audio_) audio_->queueRxAudio(pkt.data);
}

void ExtSlot::onPaTxChunk(const QByteArray& pcm)
{
    // PCM from PulseAudio: 48 kHz mono int16 LE, the same format the rest of
    // the bus uses. Forward to the mixer only while PTT is held — otherwise
    // we'd splatter the channel any time JS8Call's audio output isn't muted.
    if (!ptt_ || !mixer) return;

    // TX-meter peak (matches what virtualRig::onTxAudioFromClient does for
    // internal slots, so the control panel has consistent audio scaling).
    audioPacket out;
    out.data = pcm;
    out.seq = rxSeq_++;
    out.time = QTime::currentTime();
    out.sent = 0;
    qint16 peak = 0;
    const qint16* s = reinterpret_cast<const qint16*>(pcm.constData());
    int n = pcm.size() / 2;
    for (int i = 0; i < n; ++i) {
        qint16 v = s[i] < 0 ? (qint16)-s[i] : s[i];
        if (v > peak) peak = v;
    }
    out.amplitudePeak = peak;
    out.amplitudeRMS = 0;
    out.volume = 0;
    mixer->pushTxAudio(index(), out);
}
