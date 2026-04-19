#include "direwolfprocessor.h"
#include "logcategories.h"
#include "audio/resampler/speex_resampler.h"

#include <QAtomicPointer>
#include <QDebug>

extern "C" {
#include "direwolf.h"
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

    // Real modem init + struct audio_s population lands in M2.
    // For M1 we just confirm the link and return success.
    qCInfo(logWebServer) << "DireWolf: init skeleton (radioRate=" << radioRate_ << ")";
    return true;
}

void DireWolfProcessor::cleanup()
{
    enabled_ = false;
    channelEnabled_[0] = channelEnabled_[1] = false;
    destroyResamplers();
    txPcmBuffer.clear();
    rxAccumulator.clear();
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

void DireWolfProcessor::setChannelEnabled(int chan, bool on)
{
    if (chan >= 0 && chan < 2) channelEnabled_[chan] = on;
}

void DireWolfProcessor::processRx(audioPacket audio)
{
    (void)audio;
    // M2: downsample to modemRate, feed multi_modem_process_sample per sample.
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
    (void)subchan; (void)slice; (void)alevelMark; (void)alevelSpace;
    (void)fecType; (void)retries;
    emit rxFrame(chan, ax25, alevelRec);
    // Decoded-field decomposition (src/dst/path/info) lands in M3 once
    // we route through webserver; for now the raw bytes are enough.
}

void DireWolfProcessor::onTxByteFromC(int adev, int byte)
{
    (void)adev;
    txPcmBuffer.append(static_cast<char>(byte & 0xff));
}
