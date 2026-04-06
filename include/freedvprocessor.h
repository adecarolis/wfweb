#ifndef FREEDVPROCESSOR_H
#define FREEDVPROCESSOR_H

#include <QObject>
#include <QByteArray>
#include "audioconverter.h"

// Forward declarations - codec2/freedv
struct freedv;
// Speex resampler
typedef struct SpeexResamplerState_ SpeexResamplerState;

class FreeDVProcessor : public QObject {
    Q_OBJECT
public:
    explicit FreeDVProcessor(QObject *parent = nullptr);
    ~FreeDVProcessor();

public slots:
    bool init(int freedvMode, quint32 radioSampleRate);
    void processRx(audioPacket audio);
    void processTx(audioPacket audio);
    void setEnabled(bool enabled);
    void cleanup();

signals:
    void rxReady(audioPacket audio);
    void txReady(audioPacket audio);
    void syncChanged(bool inSync);
    void statsUpdate(float snr, bool sync);
    void rxCallsign(const QString &callsign);

private:
    void destroyResamplers();
    static void txtRxCallback(void *state, char c);
    static char txtTxCallback(void *state);

    struct freedv *fdv = nullptr;
    QString rxTextBuffer_;
    bool enabled_ = false;
    int mode_ = 0;
    quint32 radioRate_ = 0;

    // Resamplers: radio rate (e.g. 48kHz) <-> FreeDV modem rate (8kHz)
    SpeexResamplerState *rxDownsampler = nullptr;   // radioRate -> modemRate (RX input)
    SpeexResamplerState *rxUpsampler = nullptr;     // speechRate -> radioRate (RX output)
    SpeexResamplerState *txDownsampler = nullptr;   // radioRate -> speechRate (TX input)
    SpeexResamplerState *txUpsampler = nullptr;     // modemRate -> radioRate (TX output)

    // Accumulation buffers (FreeDV needs exact frame sizes)
    QByteArray rxAccumulator;
    QByteArray txAccumulator;

    // Frame sizes from FreeDV API
    int nSpeechSamples = 0;
    int nNomModemSamples = 0;
    int nMaxModemSamples = 0;
    int modemSampleRate = 0;
    int speechSampleRate = 0;
};

#endif // FREEDVPROCESSOR_H
