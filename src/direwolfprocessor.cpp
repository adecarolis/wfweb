#include "direwolfprocessor.h"
#include "logcategories.h"
#include "audio/resampler/speex_resampler.h"

#include <QAtomicPointer>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QVector>

#include <cstdio>
#include <cstring>
#include <mutex>

extern "C" {
#include "direwolf.h"
#include "audio.h"
#include "ax25_pad.h"
#include "multi_modem.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "hdlc_send.h"
#include "gen_tone.h"
#include "demod.h"
#include "dlq.h"
}

// ---------------------------------------------------------------------------
// Single-instance registry
//
// Dire Wolf's modem/HDLC core uses file-scope statics everywhere, so we
// only support one DireWolfProcessor instance at a time.  The C shims in
// wfweb_direwolf_stubs.c call the trampolines below, which dispatch into
// whatever instance is currently registered here.
// ---------------------------------------------------------------------------

static QAtomicPointer<DireWolfProcessor> g_active;

DireWolfProcessor *DireWolfProcessor::active()
{
    return g_active.loadRelaxed();
}

void DireWolfProcessor::ensureDlqInitialized()
{
    static std::once_flag dlqOnce;
    std::call_once(dlqOnce, []() { dlq_init(0); });
}

// ---------------------------------------------------------------------------
// C trampolines referenced from wfweb_direwolf_stubs.c
// ---------------------------------------------------------------------------

extern "C" void wfweb_dw_log(int level, const char *msg)
{
    if (!msg) return;
    // level maps loosely to dw_color_t (DW_COLOR_ERROR=1, DW_COLOR_INFO=2, etc.)
    if (level == 1)
        qCWarning(logWebServer) << "DireWolf:" << msg;
    else
        qCInfo(logWebServer)    << "DireWolf:" << msg;
}

extern "C" int wfweb_dw_tx_put_byte(int adev, int byte)
{
    DireWolfProcessor *p = DireWolfProcessor::active();
    if (p) p->onTxByteFromC(adev, byte);
    return byte;
}

extern "C" void wfweb_dw_rx_frame(int chan, int subchan, int slice,
                                  const unsigned char *ax25, int len,
                                  int alevel_rec, int alevel_mark,
                                  int alevel_space, int fec_type, int retries)
{
    (void)fec_type; (void)retries; (void)alevel_mark; (void)alevel_space;
    DireWolfProcessor *p = DireWolfProcessor::active();
    if (!p || !ax25 || len <= 0) return;
    QByteArray bytes(reinterpret_cast<const char *>(ax25), len);
    p->onRxFrameFromC(chan, subchan, slice, bytes,
                      alevel_rec, alevel_mark, alevel_space,
                      fec_type, retries);
}

// ---------------------------------------------------------------------------

DireWolfProcessor::DireWolfProcessor(QObject *parent)
    : QObject(parent)
{
    g_active.testAndSetRelaxed(nullptr, this);
}

DireWolfProcessor::~DireWolfProcessor()
{
    cleanup();
    g_active.testAndSetRelaxed(this, nullptr);
}

bool DireWolfProcessor::init(quint32 radioSampleRate)
{
    // The HDLC decoder pushes decoded frames into dlq — make sure it's
    // initialized before any RX processing, independent of whether the
    // AX.25 link processor has been constructed (the self-test path
    // runs the demod standalone).
    ensureDlqInitialized();

    // cleanup() clears enabled_ as part of tearing down the modem.  When
    // init() is called from setMode() after the user has already enabled
    // the modem, we need to keep the enable flag so we don't silently
    // drop every subsequent processRx() at the `!enabled_` gate.
    bool wasEnabled = enabled_;
    cleanup();
    enabled_ = wasEnabled;
    radioRate_ = radioSampleRate;

    dwCfg = new struct audio_s;
    std::memset(dwCfg, 0, sizeof(*dwCfg));

    // Single audio device, mono, 16-bit, running at modemRate_ (48 kHz).
    dwCfg->adev[0].defined = 1;
    dwCfg->adev[0].copy_from = -1;
    dwCfg->adev[0].num_channels = 1;
    dwCfg->adev[0].samples_per_sec = modemRate_;
    dwCfg->adev[0].bits_per_sample = 16;

    // Single channel (ch0) configured per current mode_.  Dire Wolf's
    // multichannel design is meant for multiple radios feeding separate
    // audio streams — on our single audio source a signal is only ever
    // one mode at a time, so we run exactly one demodulator.
    int ch = 0;
    dwCfg->chan_medium[ch] = MEDIUM_RADIO;
    if (mode_ == 9600) {
        dwCfg->achan[ch].modem_type = audio_s::achan_param_s::MODEM_SCRAMBLE;
        dwCfg->achan[ch].mark_freq = 0;
        dwCfg->achan[ch].space_freq = 0;
        dwCfg->achan[ch].baud = 9600;
        // Space-filled profile string avoids demod.c picking an AFSK default.
        std::strncpy(dwCfg->achan[ch].profiles, " ", sizeof(dwCfg->achan[ch].profiles) - 1);
    } else if (mode_ == 300) {
        // HF AFSK: 200 Hz shift centered around 1700 Hz.
        dwCfg->achan[ch].modem_type = audio_s::achan_param_s::MODEM_AFSK;
        dwCfg->achan[ch].mark_freq = 1600;
        dwCfg->achan[ch].space_freq = 1800;
        dwCfg->achan[ch].baud = 300;
        std::strncpy(dwCfg->achan[ch].profiles, "A", sizeof(dwCfg->achan[ch].profiles) - 1);
    } else {
        // Default / 1200 bps Bell 202 AFSK (VHF packet / APRS).
        mode_ = 1200;
        dwCfg->achan[ch].modem_type = audio_s::achan_param_s::MODEM_AFSK;
        dwCfg->achan[ch].mark_freq = DEFAULT_MARK_FREQ;   // 1200
        dwCfg->achan[ch].space_freq = DEFAULT_SPACE_FREQ; // 2200
        dwCfg->achan[ch].baud = 1200;
        std::strncpy(dwCfg->achan[ch].profiles, "A", sizeof(dwCfg->achan[ch].profiles) - 1);
    }
    dwCfg->achan[ch].num_freq = 1;
    dwCfg->achan[ch].offset = 0;
    dwCfg->achan[ch].fix_bits = RETRY_NONE;
    dwCfg->achan[ch].sanity_test = SANITY_APRS;
    dwCfg->achan[ch].passall = 0;
    dwCfg->achan[ch].layer2_xmit = audio_s::achan_param_s::LAYER2_AX25;
    dwCfg->achan[ch].dwait = DEFAULT_DWAIT;
    dwCfg->achan[ch].slottime = DEFAULT_SLOTTIME;
    dwCfg->achan[ch].persist = DEFAULT_PERSIST;
    dwCfg->achan[ch].txdelay = DEFAULT_TXDELAY;
    dwCfg->achan[ch].txtail = DEFAULT_TXTAIL;

    // multi_modem_init internally calls demod_init + hdlc_rec_init.
    multi_modem_init(dwCfg);
    // hdlc_rec2 has no explicit init; gen_tone_init is needed for TX only.
    gen_tone_init(dwCfg, 100, 0);

    // Speex resamplers between radio rate and modem rate.
    if (radioRate_ != 0 && (int)radioRate_ != modemRate_) {
        int err = 0;
        const int quality = 5;
        rxDownsampler = wf_resampler_init(1, radioRate_, modemRate_, quality, &err);
        txUpsampler   = wf_resampler_init(1, modemRate_, radioRate_, quality, &err);
    }

    qCInfo(logWebServer) << "DireWolf: init ok — radioRate=" << radioRate_
                         << "modemRate=" << modemRate_
                         << "mode=" << mode_;
    return true;
}

void DireWolfProcessor::cleanup()
{
    enabled_ = false;
    destroyResamplers();
    txPcmBuffer.clear();
    rxAccumulator.clear();
    rxResampleBuf.clear();
    if (dwCfg) { delete dwCfg; dwCfg = nullptr; }
}

void DireWolfProcessor::destroyResamplers()
{
    if (rxDownsampler) { wf_resampler_destroy(rxDownsampler); rxDownsampler = nullptr; }
    if (txUpsampler)   { wf_resampler_destroy(txUpsampler);   txUpsampler   = nullptr; }
}

void DireWolfProcessor::setEnabled(bool enabled)
{
    enabled_ = enabled;
}

void DireWolfProcessor::setMode(int baud)
{
    if (baud != 300 && baud != 1200 && baud != 9600) return;
    if (baud == mode_ && dwCfg) return;
    mode_ = baud;
    if (radioRate_ != 0) init(radioRate_);
}

void DireWolfProcessor::processRx(audioPacket audio)
{
    if (!enabled_ || !dwCfg) return;

    const qint16 *inSamples = reinterpret_cast<const qint16 *>(audio.data.constData());
    int inCount = audio.data.size() / (int)sizeof(qint16);
    if (inCount <= 0) return;

    // Resample to modemRate_ if the radio isn't already there.
    const qint16 *modemPtr = inSamples;
    int modemCount = inCount;
    QByteArray resampled;

    if (rxDownsampler && (int)radioRate_ != modemRate_) {
        QVector<float> inFloat(inCount);
        for (int i = 0; i < inCount; i++) inFloat[i] = inSamples[i] / 32768.0f;

        spx_uint32_t inLen = inCount;
        spx_uint32_t outLen = (spx_uint32_t)((qint64)inCount * modemRate_ / radioRate_) + 64;
        QVector<float> outFloat(outLen);
        wf_resampler_process_float(rxDownsampler, 0,
                                   inFloat.data(), &inLen,
                                   outFloat.data(), &outLen);

        resampled.resize((int)outLen * (int)sizeof(qint16));
        qint16 *out = reinterpret_cast<qint16 *>(resampled.data());
        for (spx_uint32_t i = 0; i < outLen; i++)
            out[i] = (qint16)qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);

        modemPtr = out;
        modemCount = (int)outLen;
    }

    // Feed every sample into the single active demodulator (ch0).
    for (int i = 0; i < modemCount; i++) {
        multi_modem_process_sample(0, (int)modemPtr[i]);
    }
}

void DireWolfProcessor::transmitFrame(QString monitor)
{
    if (!enabled_ || !dwCfg) {
        emit txFailed(QStringLiteral("packet modem not enabled"));
        return;
    }

    // ax25_from_text expects a mutable C string.  AX.25 addresses are
    // restricted to ASCII, and the info field is passed through verbatim
    // as bytes — so Latin-1 round-trips without loss.
    QByteArray asciiBuf = monitor.toLatin1();
    // strict=1 applies APRS-style callsign checks (6-char base + SSID).  Same
    // mode the self-test uses; loose mode (0) allows tactical calls but is
    // out of scope for the initial TX UI.
    packet_t pp = ax25_from_text(asciiBuf.data(), 1);
    if (!pp) {
        qCWarning(logWebServer) << "DireWolf TX: ax25_from_text rejected" << monitor;
        emit txFailed(QStringLiteral("malformed packet: %1").arg(monitor));
        return;
    }

    // Render the frame on the wire before keying — pp already has everything
    // we need; compute the raw bytes via ax25_pack for the hex preview.
    unsigned char frameBytesBuf[AX25_MAX_PACKET_LEN];
    int frameLen = ax25_pack(pp, frameBytesBuf);
    QByteArray frameBytes;
    if (frameLen > 0) {
        frameBytes = QByteArray(reinterpret_cast<const char *>(frameBytesBuf), frameLen);
    }
    QJsonObject monFrame = buildFrameJson(pp, frameBytes, 0, 0);
    emit txFrameDecoded(0, monFrame);

    // Clear before encoding so txPcmBuffer holds exactly this burst.
    txPcmBuffer.clear();
    // Preamble (TXDELAY), frame, postamble/flush — identical sequence to
    // the self-test.  At 1200 baud, 32 flag bytes is ~267 ms of key-up time
    // before the first data byte; comfortable margin for most radios.
    layer2_preamble_postamble(0, 32, 0, dwCfg);
    layer2_send_frame(0, pp, 0, dwCfg);
    layer2_preamble_postamble(0, 2, 1, dwCfg);
    ax25_delete(pp);

    if (txPcmBuffer.isEmpty()) {
        qCWarning(logWebServer) << "DireWolf TX: hdlc_send produced no audio for" << monitor;
        emit txFailed(QStringLiteral("TX produced no audio"));
        return;
    }

    audioPacket pkt;
    pkt.seq = 0;

    if (txUpsampler && radioRate_ != 0 && (int)radioRate_ != modemRate_) {
        // Upsample modemRate_ (48 kHz int16 LE mono) -> radioRate_.
        int inCount = txPcmBuffer.size() / (int)sizeof(qint16);
        const qint16 *inPtr = reinterpret_cast<const qint16 *>(txPcmBuffer.constData());

        QVector<float> inFloat(inCount);
        for (int i = 0; i < inCount; i++) inFloat[i] = inPtr[i] / 32768.0f;

        spx_uint32_t inLen = inCount;
        spx_uint32_t outLen = (spx_uint32_t)((qint64)inCount * radioRate_ / modemRate_) + 64;
        QVector<float> outFloat(outLen);
        wf_resampler_process_float(txUpsampler, 0,
                                   inFloat.data(), &inLen,
                                   outFloat.data(), &outLen);

        pkt.data.resize((int)outLen * (int)sizeof(qint16));
        qint16 *out = reinterpret_cast<qint16 *>(pkt.data.data());
        for (spx_uint32_t i = 0; i < outLen; i++)
            out[i] = (qint16)qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);
    } else {
        // Radio rate already matches modem rate (or no resampler configured).
        pkt.data = txPcmBuffer;
    }
    txPcmBuffer.clear();

    quint32 outRate = (radioRate_ != 0) ? radioRate_ : (quint32)modemRate_;
    qCInfo(logWebServer).noquote()
        << "DireWolf TX:" << monitor
        << QString("(%1 samples @ %2 Hz, mode=%3)")
            .arg(pkt.data.size() / (int)sizeof(qint16))
            .arg(outRate)
            .arg(mode_);

    emit txReady(pkt);
}

void DireWolfProcessor::transmitFrameBytes(int chan, int prio, QByteArray frame)
{
    if (!enabled_ || !dwCfg) {
        emit txFailed(QStringLiteral("packet modem not enabled"));
        return;
    }
    if (frame.isEmpty()) return;

    // Defer through the CSMA queue so we don't TX while the channel is busy.
    // 9600 G3RUH baseband doesn't have a meaningful DCD via the AFSK modem
    // path, but the same gating still helps avoid back-to-back collisions.
    //
    // Dedup: if this frame is a v2.0 S-frame (15 bytes — 14 addr + 1 ctrl,
    // no info, typically RR) going to the same (src,dst) as another queued
    // S-frame, the new one carries a later N(R) and supersedes the old one.
    // This prevents the receiver from piling up a chain of RRs when the
    // channel is held busy by a noisy sender retransmitting duplicates.
    if (frame.size() == 15) {
        QByteArray newAddr = frame.left(14);     // dest(7) + src(7)
        for (int i = txPendingFrames_.size() - 1; i >= 0; --i) {
            const QByteArray &old = txPendingFrames_[i].frame;
            if (old.size() == 15 && old.left(14) == newAddr) {
                txPendingFrames_.removeAt(i);
            }
        }
    }
    txPendingFrames_.enqueue({chan, prio, frame});
    if (!txCsmaTimer_) {
        txCsmaTimer_ = new QTimer(this);
        txCsmaTimer_->setTimerType(Qt::PreciseTimer);
        connect(txCsmaTimer_, &QTimer::timeout, this, &DireWolfProcessor::txCsmaTick);
    }
    if (!txCsmaTimer_->isActive()) {
        txChannelIdleSinceMs_ = 0;
        txCsmaTimer_->start(50);
    }
}

void DireWolfProcessor::txCsmaTick()
{
    if (txPendingFrames_.isEmpty()) {
        if (txCsmaTimer_) txCsmaTimer_->stop();
        return;
    }

    // hdlc_rec_data_detect_any returns non-zero while any subchannel/slice
    // sees an HDLC carrier.  At 1200 baud one full UI frame is short enough
    // that this is rarely contended, but on 300 bd HF a SABM is ~1.4 s of
    // airtime and back-to-back TX without DCD gating routinely collides.
    int busy = hdlc_rec_data_detect_any(0);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (busy) {
        txChannelIdleSinceMs_ = 0;
        return;
    }
    if (txChannelIdleSinceMs_ == 0) {
        txChannelIdleSinceMs_ = now;
        return;
    }
    // Slottime: at 300 bd we want a longer back-off because RX detection
    // lags the actual end of TX more (longer flag train).  100 ms covers
    // 1200 / 9600 comfortably.
    int slottimeMs = (mode_ == 300) ? 250 : 100;
    if (now - txChannelIdleSinceMs_ < slottimeMs) return;

    PendingTx tx = txPendingFrames_.dequeue();
    encodeAndEmitFrame(tx.frame);
    // Reset the idle window for the next pending frame so we wait one more
    // slottime after our own TX completes (DCD will go busy during our own
    // emission and clear shortly after).
    txChannelIdleSinceMs_ = 0;
}

void DireWolfProcessor::encodeAndEmitFrame(const QByteArray &frame)
{
    alevel_t a; std::memset(&a, 0, sizeof(a));
    packet_t pp = ax25_from_frame(
        reinterpret_cast<unsigned char *>(const_cast<char *>(frame.constData())),
        frame.size(), a);
    if (!pp) {
        qCWarning(logWebServer) << "DireWolf TX: ax25_from_frame failed for"
                                << frame.size() << "bytes";
        emit txFailed(QStringLiteral("ax25_from_frame failed"));
        return;
    }

    // Surface the outgoing frame to the UI monitor before we tear pp down.
    QJsonObject monFrame = buildFrameJson(pp, frame, 0, 0);
    emit txFrameDecoded(0, monFrame);

    txPcmBuffer.clear();
    layer2_preamble_postamble(0, 32, 0, dwCfg);
    layer2_send_frame(0, pp, 0, dwCfg);
    layer2_preamble_postamble(0, 2, 1, dwCfg);
    ax25_delete(pp);

    if (txPcmBuffer.isEmpty()) {
        qCWarning(logWebServer) << "DireWolf TX (bytes): hdlc_send produced no audio";
        emit txFailed(QStringLiteral("TX produced no audio"));
        return;
    }

    audioPacket pkt;
    pkt.seq = 0;

    if (txUpsampler && radioRate_ != 0 && (int)radioRate_ != modemRate_) {
        int inCount = txPcmBuffer.size() / (int)sizeof(qint16);
        const qint16 *inPtr = reinterpret_cast<const qint16 *>(txPcmBuffer.constData());

        QVector<float> inFloat(inCount);
        for (int i = 0; i < inCount; i++) inFloat[i] = inPtr[i] / 32768.0f;

        spx_uint32_t inLen = inCount;
        spx_uint32_t outLen = (spx_uint32_t)((qint64)inCount * radioRate_ / modemRate_) + 64;
        QVector<float> outFloat(outLen);
        wf_resampler_process_float(txUpsampler, 0,
                                   inFloat.data(), &inLen,
                                   outFloat.data(), &outLen);

        pkt.data.resize((int)outLen * (int)sizeof(qint16));
        qint16 *out = reinterpret_cast<qint16 *>(pkt.data.data());
        for (spx_uint32_t i = 0; i < outLen; i++)
            out[i] = (qint16)qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);
    } else {
        pkt.data = txPcmBuffer;
    }
    txPcmBuffer.clear();

    quint32 outRate = (radioRate_ != 0) ? radioRate_ : (quint32)modemRate_;
    qCInfo(logWebServer).noquote()
        << "DireWolf TX (bytes):"
        << QString("(%1 samples @ %2 Hz, mode=%3, frame=%4 B)")
            .arg(pkt.data.size() / (int)sizeof(qint16))
            .arg(outRate)
            .arg(mode_)
            .arg(frame.size());

    emit txReady(pkt);
}

QJsonObject DireWolfProcessor::buildFrameJson(void *packet,
                                              const QByteArray &rawBytes,
                                              int chan, int alevel)
{
    packet_t pp = static_cast<packet_t>(packet);
    QJsonObject frame;
    frame["chan"] = chan;
    frame["level"] = alevel;
    frame["ts"] = QDateTime::currentMSecsSinceEpoch();

    char addr[AX25_MAX_ADDR_LEN] = {0};
    if (ax25_get_num_addr(pp) >= 2) {
        ax25_get_addr_with_ssid(pp, AX25_DESTINATION, addr);
        frame["dst"] = QString::fromLatin1(addr);
        ax25_get_addr_with_ssid(pp, AX25_SOURCE, addr);
        frame["src"] = QString::fromLatin1(addr);

        QJsonArray path;
        int nAddr = ax25_get_num_addr(pp);
        for (int i = AX25_REPEATER_1; i < nAddr; i++) {
            ax25_get_addr_with_ssid(pp, i, addr);
            path.append(QString::fromLatin1(addr));
        }
        frame["path"] = path;
    }

    unsigned char *info = nullptr;
    int infoLen = ax25_get_info(pp, &info);
    if (info && infoLen > 0) {
        frame["info"] = QString::fromLatin1(reinterpret_cast<const char *>(info), infoLen);
    }

    // Frame-type label (SABM / UA / I / RR / UI ...).  ax25_frame_type()
    // carries a "TERRIBLE HACK" in Dire Wolf that MUTATES the packet's
    // modulo field when it can't decide between v2.0 and v2.2 — which
    // for any I-frame with PID 0xF0 flips the packet to modulo_128 and
    // makes layer2_send_frame emit bytes the peer cannot ACK.  Classify
    // on a clone so the caller's pp is never touched.
    packet_t ppClone = ax25_dup(pp);
    cmdres_t cr = cr_11;
    char desc[64] = {0};
    int pf = 0, nr = 0, ns = 0;
    ax25_frame_type_t ft = ppClone
        ? ax25_frame_type(ppClone, &cr, desc, &pf, &nr, &ns)
        : frame_not_AX25;
    QString typeLabel;
    switch (ft) {
        case frame_type_I:       typeLabel = QString("I N(S)=%1 N(R)=%2").arg(ns).arg(nr); break;
        case frame_type_S_RR:    typeLabel = QString("RR N(R)=%1").arg(nr); break;
        case frame_type_S_RNR:   typeLabel = QString("RNR N(R)=%1").arg(nr); break;
        case frame_type_S_REJ:   typeLabel = QString("REJ N(R)=%1").arg(nr); break;
        case frame_type_S_SREJ:  typeLabel = QString("SREJ N(R)=%1").arg(nr); break;
        case frame_type_U_SABME: typeLabel = "SABME"; break;
        case frame_type_U_SABM:  typeLabel = "SABM";  break;
        case frame_type_U_DISC:  typeLabel = "DISC";  break;
        case frame_type_U_DM:    typeLabel = "DM";    break;
        case frame_type_U_UA:    typeLabel = "UA";    break;
        case frame_type_U_FRMR:  typeLabel = "FRMR";  break;
        case frame_type_U_UI:    typeLabel = "UI";    break;
        case frame_type_U_XID:   typeLabel = "XID";   break;
        case frame_type_U_TEST:  typeLabel = "TEST";  break;
        case frame_type_U:       typeLabel = "U?";    break;
        case frame_not_AX25:     typeLabel = "?";     break;
        default:                 typeLabel = QString::fromLatin1(desc); break;
    }
    frame["ftype"] = typeLabel;
    frame["rawHex"] = QString::fromLatin1(rawBytes.toHex());
    if (ppClone) ax25_delete(ppClone);
    return frame;
}

void DireWolfProcessor::onRxFrameFromC(int chan, int subchan, int slice,
                                       const QByteArray &ax25,
                                       int alevelRec, int alevelMark,
                                       int alevelSpace, int fecType, int retries)
{
    (void)subchan; (void)slice; (void)fecType; (void)retries;
    emit rxFrame(chan, ax25, alevelRec);

    alevel_t alevel;
    std::memset(&alevel, 0, sizeof(alevel));
    alevel.rec = alevelRec;
    alevel.mark = alevelMark;
    alevel.space = alevelSpace;

    packet_t pp = ax25_from_frame(
        reinterpret_cast<unsigned char *>(const_cast<char *>(ax25.constData())),
        ax25.size(), alevel);
    if (!pp) {
        qCWarning(logWebServer) << "DireWolf: ax25_from_frame failed for"
                                << ax25.size() << "bytes on ch" << chan;
        return;
    }

    QJsonObject frame = buildFrameJson(pp, ax25, chan, alevelRec);

    qCInfo(logWebServer).noquote() << "DireWolf RX ch" << chan
                                   << frame.value("src").toString() << ">"
                                   << frame.value("dst").toString()
                                   << frame.value("ftype").toString()
                                   << "info:" << frame.value("info").toString();

    emit rxFrameDecoded(chan, frame);

    ax25_delete(pp);
}

void DireWolfProcessor::onTxByteFromC(int adev, int byte)
{
    (void)adev;
    txPcmBuffer.append(static_cast<char>(byte & 0xff));
}

// ---------------------------------------------------------------------------
// Loopback self-test
//
// Encodes a known AX.25 UI frame with Dire Wolf's TX path (hdlc_send ->
// gen_tone -> audio_put), captures the resulting int16 LE PCM into
// txPcmBuffer, then feeds those samples back through processRx so the
// demodulator decodes them.  Returns 0 on match, nonzero on failure.
// ---------------------------------------------------------------------------

int DireWolfProcessor::runSelfTestMode(int baud)
{
    const char kMonitor[] = "N0CALL>APRS:packet selftest";
    const QString kExpectedSrc  = "N0CALL";
    const QString kExpectedDst  = "APRS";
    const QString kExpectedInfo = "packet selftest";

    DireWolfProcessor dw;
    dw.mode_ = baud;
    if (!dw.init(/*radioSampleRate=*/0)) {
        qCWarning(logWebServer) << "SelfTest[" << baud << "]: init failed";
        return 2;
    }
    dw.setEnabled(true);

    QJsonObject decoded;
    bool gotFrame = false;
    QObject::connect(&dw, &DireWolfProcessor::rxFrameDecoded,
                     [&](int /*chan*/, QJsonObject frame) {
                         decoded = frame;
                         gotFrame = true;
                     });

    packet_t pp = ax25_from_text(const_cast<char *>(kMonitor), 1);
    if (!pp) {
        qCWarning(logWebServer) << "SelfTest[" << baud << "]: ax25_from_text failed";
        return 3;
    }

    // Preamble (TXDELAY), frame, postamble flush.  Use generous preamble
    // so the demodulator's PLL locks well before the start flag.
    layer2_preamble_postamble(0, 32, 0, dw.dwCfg);
    layer2_send_frame(0, pp, 0, dw.dwCfg);
    layer2_preamble_postamble(0, 2, 1, dw.dwCfg);
    ax25_delete(pp);

    // txPcmBuffer now holds int16 LE mono at modemRate_.  Feed it through
    // the RX path as an audioPacket at the same rate (radioRate==0 =>
    // no resampling).
    audioPacket pkt;
    pkt.data = dw.txPcmBuffer;
    pkt.seq = 0;
    dw.txPcmBuffer.clear();

    qCInfo(logWebServer) << "SelfTest[" << baud << "]: TX produced"
                         << pkt.data.size() / 2 << "samples at"
                         << dw.modemRate_ << "Hz";

    dw.processRx(pkt);

    // Drain the dlq.  In production, AX25LinkProcessor's dispatcher thread
    // pulls DLQ_REC_FRAME items and feeds them into wfweb_dw_rx_frame()
    // which in turn emits rxFrameDecoded.  The self-test runs the modem
    // standalone, so we replicate the minimum of that loop here.
    while (dlq_item_t *E = dlq_remove()) {
        if (E->type == DLQ_REC_FRAME && E->pp != nullptr) {
            unsigned char frame[AX25_MAX_PACKET_LEN];
            int flen = ax25_pack(E->pp, frame);
            if (flen > 0) {
                wfweb_dw_rx_frame(E->chan, E->subchan, E->slice,
                                  frame, flen,
                                  E->alevel.rec, E->alevel.mark,
                                  E->alevel.space,
                                  E->fec_type, E->retries);
            }
        }
        dlq_delete(E);
    }

    if (!gotFrame) {
        qCWarning(logWebServer) << "SelfTest[" << baud << "]: no frame decoded";
        return 4;
    }

    const QString src  = decoded.value("src").toString();
    const QString dst  = decoded.value("dst").toString();
    const QString info = decoded.value("info").toString();

    qCInfo(logWebServer).noquote()
        << "SelfTest[" << baud << "]: decoded" << src << ">" << dst
        << "info:" << info;

    if (src != kExpectedSrc || dst != kExpectedDst || info != kExpectedInfo) {
        qCWarning(logWebServer).noquote()
            << "SelfTest[" << baud << "]: field mismatch — got"
            << src << ">" << dst << "info:" << info;
        return 5;
    }

    qCInfo(logWebServer) << "SelfTest[" << baud << "]: PASS";
    return 0;
}

int DireWolfProcessor::runSelfTest()
{
    // Exercise every mode the UI exposes.  Returns 0 only if all pass;
    // on failure returns (mode * 10 + per-mode-rc) so the caller can tell
    // which mode failed.
    const int modes[] = { 300, 1200, 9600 };
    int worst = 0;
    for (int i = 0; i < 3; i++) {
        int rc = runSelfTestMode(modes[i]);
        if (rc != 0) {
            qCWarning(logWebServer) << "SelfTest: mode" << modes[i] << "FAILED rc=" << rc;
            if (worst == 0) worst = modes[i] * 10 + rc;
        }
    }
    if (worst == 0) qCInfo(logWebServer) << "SelfTest: ALL MODES PASS";
    return worst;
}
