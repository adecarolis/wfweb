#ifndef RADEPROCESSOR_H
#define RADEPROCESSOR_H

#include <QObject>
#include <QByteArray>
#include <QVector>
#include <atomic>
#include "audioconverter.h"

// Forward declarations
struct rade;
typedef struct SpeexResamplerState_ SpeexResamplerState;

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
    void cleanup();

signals:
    void rxReady(audioPacket audio);
    void txReady(audioPacket audio);
    void statsUpdate(float snr, bool sync, float freqOffset);

private:
    void destroyResamplers();
    void computeHilbertCoeffs();

    struct rade *r = nullptr;
    bool enabled_ = false;
    quint32 radioRate_ = 0;

public:
    // Cross-thread stop flag: set from webserver thread to immediately
    // halt processing without waiting for queued cleanup() to execute.
    std::atomic<bool> stopRequested{false};
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
};

#endif // RADEPROCESSOR_H
