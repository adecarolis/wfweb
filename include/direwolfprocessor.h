#ifndef DIREWOLFPROCESSOR_H
#define DIREWOLFPROCESSOR_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <QQueue>
#include <QTimer>

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
    // runSelfTest() exercises every supported mode; runSelfTestMode()
    // tests one specific baud (300, 1200, or 9600).
    static int runSelfTest();
    static int runSelfTestMode(int baud);

    // Offline demod: read a mono/stereo 16-bit PCM WAV, feed through the
    // demodulator at the given baud (300/1200/9600), print decoded frames
    // to stdout.  Returns 0 if at least one frame decoded, 1 on file/format
    // error, 2 if the WAV produced no frames.
    // Used by `wfweb --packet-decode-wav <file> [--packet-baud N]`.
    static int decodeWavFile(const QString &path, int baud);

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
    // Encode and emit a single AX.25 UI frame.  `monitor` is TNC-style
    // "SRC>DST[,PATH,...]:info" — parsed via ax25_from_text().  Emits
    // txReady(audioPacket) with int16 LE mono PCM at radioRate_ on success,
    // or txFailed(reason) if the modem is disabled / monitor is malformed.
    void transmitFrame(QString monitor);
    // Connected-mode TX entry point used by AX25LinkProcessor.  `frame`
    // is a fully-formed AX.25 frame (address + control + info + FCS — i.e.
    // exactly what ax25_pack produces).  Encoded through the same modem
    // pipeline as transmitFrame().
    void transmitFrameBytes(int chan, int prio, QByteArray frame);
    void setEnabled(bool enabled);
    // Start capturing the next `seconds` of incoming audio (as seen by the
    // demodulator, post-resample to modemRate_) to a 16-bit mono WAV file.
    // Emits captureComplete() when done or captureFailed() on error.
    void startCapture(int seconds, const QString &path);
    // Mode is the baud rate of the single active modem.  Valid values:
    //   300  — HF AFSK (mark 1600 / space 1800, 200 Hz shift)
    //   1200 — VHF AFSK (mark 1200 / space 2200, Bell 202 / APRS)
    //   9600 — VHF G3RUH scrambled baseband FSK
    // Other values are ignored.
    void setMode(int baud);
    void cleanup();

signals:
    void rxFrame(int chan, QByteArray ax25, int alevel);
    void rxFrameDecoded(int chan, QJsonObject frame);
    void txReady(audioPacket audio);
    void txFailed(QString reason);
    void stats(int chan, float level);
    void captureComplete(QString path, int sampleRate, int sampleCount);
    void captureFailed(QString reason);

private:
    void destroyResamplers();

    struct audio_s *dwCfg = nullptr;
    SpeexResamplerState *rxDownsampler = nullptr;   // radioRate -> modemRate
    SpeexResamplerState *txUpsampler = nullptr;     // modemRate -> radioRate
    QByteArray txPcmBuffer;                         // populated via audio_put
    QByteArray rxAccumulator;
    bool enabled_ = false;
    quint32 radioRate_ = 0;
    int modemRate_ = 48000;                         // common rate: works for 300/1200 AFSK + 9600 G3RUH
    int mode_ = 300;                                // 300, 1200, or 9600 — see setMode()
    QByteArray rxResampleBuf;                       // accumulated modem-rate int16 samples

    // CSMA gating for connected-mode TX.  Frames produced by ax25_link via
    // transmitFrameBytes() can pile up faster than the channel allows;
    // queue them and drain via txCsmaTimer_, gating each TX on DCD being
    // idle for at least slottime ms (P-persistence equivalent).  The APRS
    // single-shot UI path doesn't use this — it goes straight through
    // transmitFrame() and is gated externally by packetTxDraining.
    struct PendingTx { int chan; int prio; QByteArray frame; };
    QQueue<PendingTx> txPendingFrames_;
    QTimer *txCsmaTimer_ = nullptr;
    qint64  txChannelIdleSinceMs_ = 0;
    void    txCsmaTick();
    void    encodeAndEmitFrame(const QByteArray &frame);

    // Audio capture (debug aid for --packet-decode-wav round-trip testing).
    bool captureActive_ = false;
    int  captureSamplesTarget_ = 0;
    QByteArray capturePcm_;
    QString capturePath_;
};

#endif // DIREWOLFPROCESSOR_H
