#ifndef DIREWOLFPROCESSOR_H
#define DIREWOLFPROCESSOR_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QJsonObject>

#include "audioconverter.h"

// Speex resampler (shared with FreeDV/RADE)
typedef struct SpeexResamplerState_ SpeexResamplerState;

// Forward-declare Dire Wolf's audio config struct so we don't leak
// direwolf.h into the rest of wfweb.
struct audio_s;

class DireWolfProcessor : public QObject {
    Q_OBJECT
public:
    explicit DireWolfProcessor(QObject *parent = nullptr);
    ~DireWolfProcessor();

    // Dire Wolf uses process-global static state, so only one instance
    // can be active at a time.  The C shims in wfweb_direwolf_stubs.c
    // dispatch into whichever instance is currently marked active.
    static DireWolfProcessor *active();

    // In-process loopback self-test: encode a known AX.25 UI frame, feed
    // the generated audio back through the demodulator, verify the
    // decoded fields match.  Returns 0 on success, nonzero on failure.
    // Used by `wfweb --packet-self-test` and tests/test_packet.py.
    static int runSelfTest();

    // Invoked from the C shims via wfweb_dw_rx_frame / wfweb_dw_tx_put_byte.
    // Thread context: called on the DireWolf worker thread (same thread
    // the demodulator runs on), so signals are emitted there and Qt
    // queues them across to the webserver thread.
    void onRxFrameFromC(int chan, int subchan, int slice,
                        const QByteArray &ax25,
                        int alevelRec, int alevelMark, int alevelSpace,
                        int fecType, int retries);
    void onTxByteFromC(int adev, int byte);

public slots:
    bool init(quint32 radioSampleRate);
    void processRx(audioPacket audio);
    void transmitFrame(QByteArray ax25);
    void setEnabled(bool enabled);
    void setChannelEnabled(int chan, bool on);
    void cleanup();

signals:
    void rxFrame(int chan, QByteArray ax25, int alevel);
    void rxFrameDecoded(int chan, QJsonObject frame);
    void txReady(audioPacket audio);
    void stats(int chan, float level);

private:
    void destroyResamplers();

    struct audio_s *dwCfg = nullptr;
    SpeexResamplerState *rxDownsampler = nullptr;   // radioRate -> modemRate
    SpeexResamplerState *txUpsampler = nullptr;     // modemRate -> radioRate
    QByteArray txPcmBuffer;                         // populated via audio_put
    QByteArray rxAccumulator;
    bool enabled_ = false;
    quint32 radioRate_ = 0;
    int modemRate_ = 48000;                         // common rate: works for 1200 AFSK + 9600 G3RUH
    bool channelEnabled_[2] = { false, false };     // ch0 AFSK, ch1 FSK
    QByteArray rxResampleBuf;                       // accumulated modem-rate int16 samples
};

#endif // DIREWOLFPROCESSOR_H
