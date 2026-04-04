#include "radeprocessor.h"
#include "logcategories.h"
#include "audio/resampler/speex_resampler.h"

extern "C" {
#include <rade_api.h>
#include <rade_dsp.h>
#include <lpcnet.h>
#include <fargan.h>
#include <arch.h>
#include <cpu_support.h>
}

#include <QTime>
#include <QtMath>
#include <cstring>

// Constants from rade_dsp.h
static const int SPEECH_RATE      = RADE_FS_SPEECH;    // 16000
static const int MODEM_RATE       = RADE_FS;           // 8000
static const int LPCNET_FRAME_SZ  = LPCNET_FRAME_SIZE; // 160 samples (10ms at 16kHz)

RadeProcessor::RadeProcessor(QObject *parent)
    : QObject(parent)
{
}

RadeProcessor::~RadeProcessor()
{
    cleanup();
}

void RadeProcessor::computeHilbertCoeffs()
{
    int center = HILBERT_DELAY;
    for (int i = 0; i < HILBERT_NTAPS; i++) {
        int n = i - center;
        if (n == 0 || (n & 1) == 0) {
            hilbertCoeffs[i] = 0.0f;
        } else {
            float h = 2.0f / (M_PI * n);
            float w = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (HILBERT_NTAPS - 1));
            hilbertCoeffs[i] = h * w;
        }
    }
}

bool RadeProcessor::init(quint32 radioSampleRate)
{
    cleanup();
    radioRate_ = radioSampleRate;

    // Initialize RADE global state (safe to call multiple times)
    rade_initialize();

    r = rade_open(NULL, RADE_USE_C_ENCODER | RADE_USE_C_DECODER);
    if (!r) {
        qWarning() << "RADE: failed to open";
        return false;
    }

    // Query sizes from API
    nFeaturesInOut = rade_n_features_in_out(r);
    nTxOut = rade_n_tx_out(r);
    nTxEooOut = rade_n_tx_eoo_out(r);
    nEooBits = rade_n_eoo_bits(r);
    ninMax = rade_nin_max(r);

    framesPerMf = nFeaturesInOut / RADE_NB_TOTAL_FEATURES;

    // Create LPCNet encoder for TX feature extraction
    lpcnetEnc = lpcnet_encoder_create();
    if (!lpcnetEnc) {
        qWarning() << "RADE: failed to create LPCNet encoder";
        cleanup();
        return false;
    }
    archFlags = opus_select_arch();

    // Create FARGAN vocoder for RX speech synthesis
    fargan = (FARGANState *)calloc(1, sizeof(FARGANState));
    if (!fargan) {
        qWarning() << "RADE: failed to allocate FARGAN state";
        cleanup();
        return false;
    }
    fargan_init(fargan);

    // Create resamplers
    int err;
    const int quality = 5;

    if (radioRate_ != (quint32)MODEM_RATE) {
        rxDownsampler = wf_resampler_init(1, radioRate_, MODEM_RATE, quality, &err);
        txUpsampler = wf_resampler_init(1, MODEM_RATE, radioRate_, quality, &err);
    }
    if (radioRate_ != (quint32)SPEECH_RATE) {
        rxUpsampler = wf_resampler_init(1, SPEECH_RATE, radioRate_, quality, &err);
        txDownsampler = wf_resampler_init(1, radioRate_, SPEECH_RATE, quality, &err);
    }

    // Hilbert transform coefficients
    computeHilbertCoeffs();
    memset(hilbertHistory, 0, sizeof(hilbertHistory));
    hilbertHistIdx = 0;

    // TX feature buffer
    txFeatureBuf.resize(nFeaturesInOut);
    txFeatureBuf.fill(0.0f);
    txFeatIdx = 0;

    // RX FARGAN warmup
    farganWarmupBuf.resize(5 * RADE_NB_TOTAL_FEATURES);
    farganWarmupBuf.fill(0.0f);
    farganReady = false;
    farganWarmupFrames = 0;

    rxAccumulator.clear();
    txAccumulator.clear();
    stopRequested.store(false);
    enabled_ = true;

    qInfo() << "RADE: initialized, modemRate=" << MODEM_RATE
            << "speechRate=" << SPEECH_RATE
            << "framesPerMf=" << framesPerMf
            << "nFeaturesInOut=" << nFeaturesInOut
            << "nTxOut=" << nTxOut
            << "ninMax=" << ninMax
            << "radioRate=" << radioSampleRate;

    return true;
}

void RadeProcessor::cleanup()
{
    enabled_ = false;
    if (r) {
        rade_close(r);
        r = nullptr;
    }
    if (lpcnetEnc) {
        lpcnet_encoder_destroy(lpcnetEnc);
        lpcnetEnc = nullptr;
    }
    if (fargan) {
        free(fargan);
        fargan = nullptr;
    }
    destroyResamplers();
    rxAccumulator.clear();
    txAccumulator.clear();
    txFeatureBuf.fill(0.0f);
    txFeatIdx = 0;
    farganReady = false;
    farganWarmupFrames = 0;
}

void RadeProcessor::destroyResamplers()
{
    if (rxDownsampler) { wf_resampler_destroy(rxDownsampler); rxDownsampler = nullptr; }
    if (rxUpsampler) { wf_resampler_destroy(rxUpsampler); rxUpsampler = nullptr; }
    if (txDownsampler) { wf_resampler_destroy(txDownsampler); txDownsampler = nullptr; }
    if (txUpsampler) { wf_resampler_destroy(txUpsampler); txUpsampler = nullptr; }
}

void RadeProcessor::setEnabled(bool enabled)
{
    enabled_ = enabled;
}

void RadeProcessor::processRx(audioPacket audio)
{
    if (!enabled_ || stopRequested.load(std::memory_order_relaxed) || !r || !fargan) return;

    const qint16 *inSamples = reinterpret_cast<const qint16 *>(audio.data.constData());
    int inCount = audio.data.size() / (int)sizeof(qint16);

    // Step 1: Resample radio rate -> 8kHz modem rate
    QVector<float> modemFloat;
    int modemCount;

    if (rxDownsampler) {
        spx_uint32_t inLen = inCount;
        spx_uint32_t outLen = (spx_uint32_t)(inCount * (double)MODEM_RATE / radioRate_) + 64;
        QVector<float> inFloat(inCount);
        for (int i = 0; i < inCount; i++)
            inFloat[i] = inSamples[i] / 32768.0f;

        modemFloat.resize(outLen);
        wf_resampler_process_float(rxDownsampler, 0, inFloat.data(), &inLen, modemFloat.data(), &outLen);
        modemCount = outLen;
        modemFloat.resize(modemCount);
    } else {
        modemCount = inCount;
        modemFloat.resize(modemCount);
        for (int i = 0; i < modemCount; i++)
            modemFloat[i] = inSamples[i] / 32768.0f;
    }

    // Step 2: Hilbert transform (real -> IQ complex)
    // Produces one RADE_COMP per input sample
    int iqBytes = modemCount * (int)sizeof(RADE_COMP);
    QByteArray iqBuf(iqBytes, 0);
    RADE_COMP *iq = reinterpret_cast<RADE_COMP *>(iqBuf.data());

    for (int i = 0; i < modemCount; i++) {
        float sample = modemFloat[i];

        // Update circular history buffer
        hilbertHistory[hilbertHistIdx] = sample;

        // Compute imaginary part via FIR convolution
        float imag = 0.0f;
        int idx = hilbertHistIdx;
        for (int k = 0; k < HILBERT_NTAPS; k++) {
            imag += hilbertCoeffs[k] * hilbertHistory[idx];
            idx--;
            if (idx < 0) idx = HILBERT_NTAPS - 1;
        }

        // Real part is delayed by HILBERT_DELAY samples
        int delayIdx = hilbertHistIdx - HILBERT_DELAY;
        if (delayIdx < 0) delayIdx += HILBERT_NTAPS;
        float real = hilbertHistory[delayIdx];

        iq[i].real = real;
        iq[i].imag = imag;

        hilbertHistIdx = (hilbertHistIdx + 1) % HILBERT_NTAPS;
    }

    // Step 3: Accumulate IQ samples
    rxAccumulator.append(iqBuf);

    // Step 4: Process complete frames via rade_rx
    int nin = rade_nin(r);
    while (rxAccumulator.size() >= nin * (int)sizeof(RADE_COMP)) {
        RADE_COMP *rxIn = reinterpret_cast<RADE_COMP *>(rxAccumulator.data());

        QVector<float> featOut(nFeaturesInOut);
        QVector<float> eooBits(nEooBits);
        int hasEoo = 0;

        int nOut = rade_rx(r, featOut.data(), &hasEoo, eooBits.data(), rxIn);

        // Remove consumed samples
        rxAccumulator.remove(0, nin * sizeof(RADE_COMP));

        // Update nin for next iteration (dynamic, like freedv_nin)
        nin = rade_nin(r);

        if (nOut > 0) {
            int nFrames = nOut / RADE_NB_TOTAL_FEATURES;

            for (int f = 0; f < nFrames; f++) {
                float *feat = featOut.data() + f * RADE_NB_TOTAL_FEATURES;

                if (!farganReady) {
                    // Warmup: accumulate first 5 frames
                    memcpy(farganWarmupBuf.data() + farganWarmupFrames * RADE_NB_TOTAL_FEATURES,
                           feat, RADE_NB_TOTAL_FEATURES * sizeof(float));
                    farganWarmupFrames++;

                    if (farganWarmupFrames >= 5) {
                        // Pack features at stride NB_FEATURES (not NB_TOTAL_FEATURES)
                        QVector<float> packed(5 * NB_FEATURES, 0.0f);
                        for (int i = 0; i < 5; i++)
                            memcpy(packed.data() + i * NB_FEATURES,
                                   farganWarmupBuf.data() + i * RADE_NB_TOTAL_FEATURES,
                                   NB_FEATURES * sizeof(float));

                        float zeros[LPCNET_FRAME_SZ] = {0};
                        fargan_cont(fargan, zeros, packed.data());
                        farganReady = true;
                    }
                    continue;
                }

                // Synthesize speech from features
                float fpcm[LPCNET_FRAME_SZ];
                fargan_synthesize(fargan, fpcm, feat);

                // Convert float -> int16
                QVector<qint16> speechOut(LPCNET_FRAME_SZ);
                for (int s = 0; s < LPCNET_FRAME_SZ; s++) {
                    float v = fpcm[s] * 32768.0f;
                    if (v > 32767.0f) v = 32767.0f;
                    if (v < -32767.0f) v = -32767.0f;
                    speechOut[s] = (qint16)floor(0.5 + (double)v);
                }

                // Resample 16kHz -> radio rate
                QByteArray speechData;
                if (rxUpsampler) {
                    spx_uint32_t inLen = LPCNET_FRAME_SZ;
                    spx_uint32_t outLen = (spx_uint32_t)(LPCNET_FRAME_SZ * (double)radioRate_ / SPEECH_RATE) + 64;
                    QVector<float> inFloat(LPCNET_FRAME_SZ);
                    for (int i = 0; i < LPCNET_FRAME_SZ; i++)
                        inFloat[i] = speechOut[i] / 32768.0f;

                    QVector<float> outFloat(outLen);
                    wf_resampler_process_float(rxUpsampler, 0, inFloat.data(), &inLen, outFloat.data(), &outLen);

                    speechData.resize(outLen * sizeof(qint16));
                    qint16 *out = reinterpret_cast<qint16 *>(speechData.data());
                    for (spx_uint32_t i = 0; i < outLen; i++)
                        out[i] = qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);
                } else {
                    speechData = QByteArray(reinterpret_cast<const char *>(speechOut.data()),
                                            LPCNET_FRAME_SZ * sizeof(qint16));
                }

                audioPacket out;
                out.data = speechData;
                out.time = QTime::currentTime();
                out.seq = audio.seq;
                out.sent = 0;
                out.volume = audio.volume;
                emit rxReady(out);
            }
        }

        // Emit stats
        bool sync = rade_sync(r) != 0;
        float snr = (float)rade_snrdB_3k_est(r);
        float foff = rade_freq_offset(r);
        emit statsUpdate(snr, sync, foff);

        // Reset FARGAN on loss of sync
        if (!sync && farganReady) {
            fargan_init(fargan);
            farganReady = false;
            farganWarmupFrames = 0;
        }
    }
}

void RadeProcessor::processTx(audioPacket audio)
{
    if (!enabled_ || stopRequested.load(std::memory_order_relaxed) || !r || !lpcnetEnc) return;

    const qint16 *inSamples = reinterpret_cast<const qint16 *>(audio.data.constData());
    int inCount = audio.data.size() / (int)sizeof(qint16);

    // Step 1: Resample radio rate -> 16kHz speech rate
    QByteArray speechBuf;
    if (txDownsampler) {
        spx_uint32_t inLen = inCount;
        spx_uint32_t outLen = (spx_uint32_t)(inCount * (double)SPEECH_RATE / radioRate_) + 64;
        QVector<float> inFloat(inCount);
        for (int i = 0; i < inCount; i++)
            inFloat[i] = inSamples[i] / 32768.0f;

        QVector<float> outFloat(outLen);
        wf_resampler_process_float(txDownsampler, 0, inFloat.data(), &inLen, outFloat.data(), &outLen);

        speechBuf.resize(outLen * sizeof(qint16));
        qint16 *out = reinterpret_cast<qint16 *>(speechBuf.data());
        for (spx_uint32_t i = 0; i < outLen; i++)
            out[i] = qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);
    } else {
        speechBuf = audio.data;
    }

    // Step 2: Accumulate speech samples
    txAccumulator.append(speechBuf);

    // Step 3: Process 10ms frames (160 samples at 16kHz)
    while (txAccumulator.size() >= LPCNET_FRAME_SZ * (int)sizeof(qint16)) {
        const qint16 *pcm = reinterpret_cast<const qint16 *>(txAccumulator.constData());

        // Extract features for this 10ms frame
        // lpcnet_compute_single_frame_features expects opus_int16 (= int16_t)
        lpcnet_compute_single_frame_features(lpcnetEnc,
            const_cast<opus_int16 *>(reinterpret_cast<const opus_int16 *>(pcm)),
            txFeatureBuf.data() + txFeatIdx * RADE_NB_TOTAL_FEATURES,
            archFlags);

        txAccumulator.remove(0, LPCNET_FRAME_SZ * sizeof(qint16));
        txFeatIdx++;

        // When we have enough feature frames, encode a modem frame
        if (txFeatIdx >= framesPerMf) {
            QVector<RADE_COMP> iqOut(nTxOut);
            int nOut = rade_tx(r, iqOut.data(), txFeatureBuf.data());
            txFeatIdx = 0;

            if (nOut > 0) {
                // Convert complex IQ -> real modem PCM (extract real part)
                QVector<qint16> modemOut(nOut);
                for (int i = 0; i < nOut; i++) {
                    float v = iqOut[i].real * 32768.0f;
                    if (v > 32767.0f) v = 32767.0f;
                    if (v < -32767.0f) v = -32767.0f;
                    modemOut[i] = (qint16)v;
                }

                // Resample 8kHz -> radio rate
                QByteArray modemData;
                if (txUpsampler) {
                    spx_uint32_t inLen = nOut;
                    spx_uint32_t outLen = (spx_uint32_t)(nOut * (double)radioRate_ / MODEM_RATE) + 64;
                    QVector<float> inFloat(nOut);
                    for (int i = 0; i < nOut; i++)
                        inFloat[i] = modemOut[i] / 32768.0f;

                    QVector<float> outFloat(outLen);
                    wf_resampler_process_float(txUpsampler, 0, inFloat.data(), &inLen, outFloat.data(), &outLen);

                    modemData.resize(outLen * sizeof(qint16));
                    qint16 *out = reinterpret_cast<qint16 *>(modemData.data());
                    for (spx_uint32_t i = 0; i < outLen; i++)
                        out[i] = qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);
                } else {
                    modemData = QByteArray(reinterpret_cast<const char *>(modemOut.data()),
                                           nOut * sizeof(qint16));
                }

                audioPacket out;
                out.data = modemData;
                out.time = QTime::currentTime();
                out.seq = audio.seq;
                out.sent = 0;
                out.volume = audio.volume;
                emit txReady(out);
            }
        }
    }
}
