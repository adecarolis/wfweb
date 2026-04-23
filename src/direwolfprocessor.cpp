#include "direwolfprocessor.h"
#include "logcategories.h"
#include "audio/resampler/speex_resampler.h"

#include <QAtomicPointer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QVector>

#include <cstdio>
#include <cstring>

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

    // Opportunistic audio capture for offline debugging.  Grab samples
    // post-resample (at modemRate_) so what we save is exactly what the
    // demodulator sees — replayable via `wfweb --packet-decode-wav`.
    if (captureActive_) {
        int want = captureSamplesTarget_ - (capturePcm_.size() / (int)sizeof(qint16));
        int take = qMin(want, modemCount);
        if (take > 0) {
            capturePcm_.append(reinterpret_cast<const char *>(modemPtr),
                               take * (int)sizeof(qint16));
        }
        if (capturePcm_.size() / (int)sizeof(qint16) >= captureSamplesTarget_) {
            captureActive_ = false;

            // Write 16-bit mono RIFF/WAVE at modemRate_.
            QFile f(capturePath_);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                emit captureFailed(QString("cannot open %1").arg(capturePath_));
                capturePcm_.clear();
                return;
            }
            auto u32 = [&](quint32 v) { char b[4] = {
                (char)(v & 0xff), (char)((v >> 8) & 0xff),
                (char)((v >> 16) & 0xff), (char)((v >> 24) & 0xff) };
                f.write(b, 4); };
            auto u16 = [&](quint16 v) { char b[2] = {
                (char)(v & 0xff), (char)((v >> 8) & 0xff) };
                f.write(b, 2); };
            int nBytes = capturePcm_.size();
            f.write("RIFF", 4);  u32(36 + nBytes);
            f.write("WAVE", 4);
            f.write("fmt ", 4);  u32(16); u16(1); u16(1);
            u32((quint32)modemRate_);        u32((quint32)modemRate_ * 2);
            u16(2);              u16(16);
            f.write("data", 4);  u32((quint32)nBytes);
            f.write(capturePcm_);
            f.close();

            int samples = capturePcm_.size() / (int)sizeof(qint16);
            qCInfo(logWebServer) << "DireWolf: capture wrote"
                                 << capturePath_ << samples << "samples @"
                                 << modemRate_ << "Hz";
            capturePcm_.clear();
            emit captureComplete(capturePath_, modemRate_, samples);
        }
    }
}

void DireWolfProcessor::startCapture(int seconds, const QString &path)
{
    if (captureActive_) {
        emit captureFailed("capture already in progress");
        return;
    }
    if (!enabled_ || !dwCfg) {
        emit captureFailed("packet modem not enabled");
        return;
    }
    if (seconds <= 0 || seconds > 60) seconds = 10;
    capturePath_ = path;
    capturePcm_.clear();
    capturePcm_.reserve(seconds * modemRate_ * (int)sizeof(qint16));
    captureSamplesTarget_ = seconds * modemRate_;
    captureActive_ = true;
    qCInfo(logWebServer) << "DireWolf: capturing" << seconds
                         << "s @" << modemRate_ << "Hz ->" << path;
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
    (void)chan; (void)prio;

    if (!enabled_ || !dwCfg) {
        emit txFailed(QStringLiteral("packet modem not enabled"));
        return;
    }
    if (frame.isEmpty()) {
        emit txFailed(QStringLiteral("empty frame"));
        return;
    }

    // Reconstruct a packet_t from the raw AX.25 bytes produced by ax25_pack
    // on the AX25LinkProcessor side.  alevel is meaningless for TX so pass 0.
    alevel_t a; std::memset(&a, 0, sizeof(a));
    packet_t pp = ax25_from_frame(
        reinterpret_cast<unsigned char *>(frame.data()), frame.size(), a);
    if (!pp) {
        qCWarning(logWebServer) << "DireWolf TX: ax25_from_frame failed for"
                                << frame.size() << "bytes";
        emit txFailed(QStringLiteral("ax25_from_frame failed"));
        return;
    }

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

    char addr[AX25_MAX_ADDR_LEN] = {0};
    QJsonObject frame;
    frame["chan"] = chan;
    frame["level"] = alevelRec;
    frame["ts"] = QDateTime::currentMSecsSinceEpoch();

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
    frame["rawHex"] = QString::fromLatin1(ax25.toHex());

    qCInfo(logWebServer).noquote() << "DireWolf RX ch" << chan
                                   << frame.value("src").toString() << ">"
                                   << frame.value("dst").toString()
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

// ---------------------------------------------------------------------------
// Offline WAV decoder (--packet-decode-wav)
//
// Minimal RIFF/WAVE reader: 16-bit PCM, mono or stereo (downmixed).
// Anything else is rejected with an error.  No libsndfile dependency —
// we only need enough to run Dire Wolf's TNC test corpus through processRx().
// ---------------------------------------------------------------------------

namespace {

// Read a little-endian unsigned integer of the given size (2 or 4 bytes).
quint32 readLE(const QByteArray &buf, int off, int size)
{
    quint32 v = 0;
    for (int i = 0; i < size; i++)
        v |= (quint32)(quint8)buf.at(off + i) << (8 * i);
    return v;
}

// Parse WAV header, populate sampleRate / numChannels, return PCM int16
// mono samples (stereo is averaged to mono).  Returns empty on error.
QByteArray loadWavInt16Mono(const QString &path, quint32 *sampleRateOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "decode-wav: cannot open '%s'\n",
                     qUtf8Printable(path));
        return {};
    }
    QByteArray all = f.readAll();
    f.close();

    if (all.size() < 44 || all.left(4) != "RIFF" || all.mid(8, 4) != "WAVE") {
        std::fprintf(stderr, "decode-wav: not a RIFF/WAVE file\n");
        return {};
    }

    // Scan chunks after the "WAVE" tag.  We need "fmt " and "data".
    int pos = 12;
    int fmtOff = -1, fmtLen = 0;
    int dataOff = -1, dataLen = 0;
    while (pos + 8 <= all.size()) {
        QByteArray id = all.mid(pos, 4);
        int len = (int)readLE(all, pos + 4, 4);
        int body = pos + 8;
        if (id == "fmt ")  { fmtOff = body;  fmtLen = len; }
        if (id == "data")  { dataOff = body; dataLen = len; break; }
        pos = body + len + (len & 1);   // chunks are word-aligned
    }
    if (fmtOff < 0 || dataOff < 0 || fmtLen < 16) {
        std::fprintf(stderr, "decode-wav: missing fmt or data chunk\n");
        return {};
    }

    quint16 audioFormat  = (quint16)readLE(all, fmtOff + 0,  2);
    quint16 numChannels  = (quint16)readLE(all, fmtOff + 2,  2);
    quint32 sampleRate   =          readLE(all, fmtOff + 4,  4);
    quint16 bitsPerSample= (quint16)readLE(all, fmtOff + 14, 2);

    if (audioFormat != 1 || bitsPerSample != 16 ||
        (numChannels != 1 && numChannels != 2)) {
        std::fprintf(stderr,
            "decode-wav: unsupported format (fmt=%u ch=%u bits=%u) "
            "— need 16-bit PCM mono/stereo\n",
            audioFormat, numChannels, bitsPerSample);
        return {};
    }

    QByteArray pcm = all.mid(dataOff, dataLen);
    if (numChannels == 2) {
        // Downmix stereo -> mono by averaging.
        int frames = pcm.size() / 4;
        QByteArray mono;
        mono.resize(frames * (int)sizeof(qint16));
        const qint16 *in  = reinterpret_cast<const qint16 *>(pcm.constData());
        qint16       *out = reinterpret_cast<qint16 *>(mono.data());
        for (int i = 0; i < frames; i++)
            out[i] = (qint16)(((qint32)in[2*i] + (qint32)in[2*i+1]) / 2);
        pcm = mono;
    }

    if (sampleRateOut) *sampleRateOut = sampleRate;
    return pcm;
}

} // namespace

int DireWolfProcessor::decodeWavFile(const QString &path, int baud)
{
    if (baud != 300 && baud != 1200 && baud != 9600) {
        std::fprintf(stderr,
            "decode-wav: invalid baud %d (expected 300, 1200, or 9600)\n",
            baud);
        return 1;
    }

    quint32 wavRate = 0;
    QByteArray pcm = loadWavInt16Mono(path, &wavRate);
    if (pcm.isEmpty()) return 1;

    QFileInfo fi(path);
    std::fprintf(stderr,
        "decode-wav: %s — %d samples @ %u Hz, baud=%d\n",
        qUtf8Printable(fi.fileName()),
        (int)(pcm.size() / (int)sizeof(qint16)), wavRate, baud);

    DireWolfProcessor dw;
    dw.mode_ = baud;
    if (!dw.init(wavRate)) {
        std::fprintf(stderr, "decode-wav: init failed\n");
        return 1;
    }
    dw.setEnabled(true);

    int frameCount = 0;
    QObject::connect(&dw, &DireWolfProcessor::rxFrameDecoded,
                     [&](int chan, QJsonObject frame) {
                         frameCount++;
                         QJsonDocument doc(frame);
                         std::fprintf(stdout, "frame[%d] chan=%d %s\n",
                             frameCount, chan,
                             doc.toJson(QJsonDocument::Compact).constData());
                         std::fflush(stdout);
                     });

    // Feed the full WAV in one shot — processRx handles resampling to
    // modemRate_ internally via Speex.
    audioPacket pkt;
    pkt.data = pcm;
    pkt.seq = 0;
    dw.processRx(pkt);

    std::fprintf(stderr, "decode-wav: %d frame(s) decoded\n", frameCount);
    return frameCount > 0 ? 0 : 2;
}
