#include "direwolfprocessor.h"
#include "logcategories.h"
#include "audio/resampler/speex_resampler.h"

#include <QAtomicPointer>
#include <QDebug>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

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
    cleanup();
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

void DireWolfProcessor::transmitFrame(QByteArray ax25)
{
    (void)ax25;
    // M5: pack into packet_t, call layer2_send_frame, emit txReady as
    //     audio_put fills the buffer.
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
