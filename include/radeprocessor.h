#ifndef RADEPROCESSOR_H
#define RADEPROCESSOR_H

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QVector>
#include <atomic>
#include "audioconverter.h"

// Forward declarations
struct rade;
typedef struct SpeexResamplerState_ SpeexResamplerState;
typedef void *rade_text_t;

// Opaque type from custom Opus (LPCNet)
struct LPCNetEncState;

// FARGANState is a typedef'd struct in fargan.h; we need the full
// definition for sizeof, so include it rather than forward-declaring.
extern "C" {
#include <fargan.h>
}

class RadeProcessor : public QObject {
    Q_OBJECT
public:
    explicit RadeProcessor(QObject *parent = nullptr);
    ~RadeProcessor();

public slots:
    bool init(quint32 radioSampleRate);
    void processRx(audioPacket audio);
    void processTx(audioPacket audio);
    void setEnabled(bool enabled);
    void setTxCallsign(const QString &callsign);
    void sendEoo();
    Q_INVOKABLE QByteArray generateEooAudio();
    void cleanup();

signals:
    void rxReady(audioPacket audio);
    void txReady(audioPacket audio);
    void statsUpdate(float snr, bool sync, float freqOffset);
    void rxCallsign(const QString &callsign);

private:
    void destroyResamplers();
    void computeHilbertCoeffs();
    void prepareTxEooBits();
    static void radeTextRxCallback(rade_text_t rt, const char *txt,
                                   int length, void *state);

    struct rade *r = nullptr;
    rade_text_t radeText = nullptr;
    bool enabled_ = false;
    quint32 radioRate_ = 0;

public:
    // Cross-thread stop flag: set from webserver thread to immediately
    // halt processing without waiting for queued cleanup() to execute.
    std::atomic<bool> stopRequested{false};

    // EOO audio result: written by generateEooAudio() on the RADE thread,
    // read by webserver after BlockingQueuedConnection returns.
    QByteArray eooAudioResult;

private:

    // LPCNet encoder (TX: PCM -> features)
    LPCNetEncState *lpcnetEnc = nullptr;
    int archFlags = 0;

    // FARGAN vocoder (RX: features -> PCM)
    FARGANState *fargan = nullptr;
    bool farganReady = false;
    int farganWarmupFrames = 0;

    // Resamplers: radio rate (48kHz) <-> RADE rates
    SpeexResamplerState *rxDownsampler = nullptr;   // radioRate -> 8k (modem in)
    SpeexResamplerState *rxUpsampler = nullptr;     // 16k -> radioRate (speech out)
    SpeexResamplerState *txDownsampler = nullptr;   // radioRate -> 16k (speech in)
    SpeexResamplerState *txUpsampler = nullptr;     // 8k -> radioRate (modem out)

    // Accumulation buffers
    QByteArray rxAccumulator;   // IQ samples (RADE_COMP) for rade_rx
    QByteArray txAccumulator;   // int16 speech samples at 16kHz for LPCNet

    // Hilbert transform (RX: real -> IQ)
    static const int HILBERT_NTAPS = 127;
    static const int HILBERT_DELAY = 63;  // (NTAPS-1)/2
    float hilbertCoeffs[HILBERT_NTAPS];
    float hilbertHistory[HILBERT_NTAPS];
    int hilbertHistIdx = 0;

    // TX feature accumulation
    QVector<float> txFeatureBuf;
    int txFeatIdx = 0;          // feature frames accumulated so far
    int framesPerMf = 0;        // feature frames per modem frame (typically 12)

    // FARGAN warmup buffer (RX)
    QVector<float> farganWarmupBuf;

    // RADE API sizes (queried at init)
    int nFeaturesInOut = 0;     // total floats per rade_tx/rx call
    int nTxOut = 0;             // complex IQ samples per rade_tx call
    int nTxEooOut = 0;          // complex IQ samples per rade_tx_eoo call
    int nEooBits = 0;           // soft-decision bits in EOO
    int ninMax = 0;             // max IQ samples rade_rx can consume

    // TX callsign for EOO encoding
    QString txCallsign_;
    QMutex callsignMutex_;
    bool txEooPrepared = false;
};

#endif // RADEPROCESSOR_H
