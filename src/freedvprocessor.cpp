#include "freedvprocessor.h"
#include "logcategories.h"
#include "audio/resampler/speex_resampler.h"

#include <codec2/freedv_api.h>
#include <codec2/modem_stats.h>

#include <QTime>
#include <QtMath>

FreeDVProcessor::FreeDVProcessor(QObject *parent)
    : QObject(parent)
{
}

FreeDVProcessor::~FreeDVProcessor()
{
    cleanup();
}

bool FreeDVProcessor::init(int freedvMode, quint32 radioSampleRate)
{
    cleanup();

    mode_ = freedvMode;
    radioRate_ = radioSampleRate;

    fdv = freedv_open(freedvMode);
    if (!fdv) {
        qWarning() << "FreeDV: failed to open mode" << freedvMode;
        return false;
    }

    nSpeechSamples = freedv_get_n_speech_samples(fdv);
    nNomModemSamples = freedv_get_n_nom_modem_samples(fdv);
    nMaxModemSamples = freedv_get_n_max_modem_samples(fdv);
    modemSampleRate = freedv_get_modem_sample_rate(fdv);
    speechSampleRate = freedv_get_speech_sample_rate(fdv);

    // Enable squelch with a reasonable default
    freedv_set_squelch_en(fdv, 1);
    freedv_set_snr_squelch_thresh(fdv, -2.0f);

    // Register text callback for callsign extraction
    freedv_set_callback_txt(fdv, &FreeDVProcessor::txtRxCallback,
                            &FreeDVProcessor::txtTxCallback, this);
    rxTextBuffer_.clear();

    qInfo() << "FreeDV: initialized mode" << freedvMode
            << "modemRate=" << modemSampleRate
            << "speechRate=" << speechSampleRate
            << "nSpeechSamples=" << nSpeechSamples
            << "nNomModemSamples=" << nNomModemSamples
            << "radioRate=" << radioSampleRate;

    // Create resamplers if radio rate differs from FreeDV rates
    int err;
    const int quality = 5;

    if (radioRate_ != (quint32)modemSampleRate) {
        rxDownsampler = wf_resampler_init(1, radioRate_, modemSampleRate, quality, &err);
        txUpsampler = wf_resampler_init(1, modemSampleRate, radioRate_, quality, &err);
    }
    if (radioRate_ != (quint32)speechSampleRate) {
        rxUpsampler = wf_resampler_init(1, speechSampleRate, radioRate_, quality, &err);
        txDownsampler = wf_resampler_init(1, radioRate_, speechSampleRate, quality, &err);
    }

    rxAccumulator.clear();
    txAccumulator.clear();
    enabled_ = true;

    return true;
}

void FreeDVProcessor::cleanup()
{
    enabled_ = false;
    if (fdv) {
        freedv_close(fdv);
        fdv = nullptr;
    }
    destroyResamplers();
    rxAccumulator.clear();
    txAccumulator.clear();
}

void FreeDVProcessor::destroyResamplers()
{
    if (rxDownsampler) { wf_resampler_destroy(rxDownsampler); rxDownsampler = nullptr; }
    if (rxUpsampler) { wf_resampler_destroy(rxUpsampler); rxUpsampler = nullptr; }
    if (txDownsampler) { wf_resampler_destroy(txDownsampler); txDownsampler = nullptr; }
    if (txUpsampler) { wf_resampler_destroy(txUpsampler); txUpsampler = nullptr; }
}

void FreeDVProcessor::setEnabled(bool enabled)
{
    enabled_ = enabled;
}

void FreeDVProcessor::processRx(audioPacket audio)
{
    if (!enabled_ || !fdv) return;

    const qint16 *inSamples = reinterpret_cast<const qint16 *>(audio.data.constData());
    int inCount = audio.data.size() / (int)sizeof(qint16);

    // Resample radio rate -> modem rate if needed
    QByteArray modemBuf;
    if (rxDownsampler) {
        // Estimate output size
        spx_uint32_t inLen = inCount;
        spx_uint32_t outLen = (spx_uint32_t)(inCount * (double)modemSampleRate / radioRate_) + 64;
        // Convert int16 -> float for resampler
        QVector<float> inFloat(inCount);
        for (int i = 0; i < inCount; i++)
            inFloat[i] = inSamples[i] / 32768.0f;

        QVector<float> outFloat(outLen);
        wf_resampler_process_float(rxDownsampler, 0, inFloat.data(), &inLen, outFloat.data(), &outLen);

        // Convert float -> int16
        modemBuf.resize(outLen * sizeof(qint16));
        qint16 *out = reinterpret_cast<qint16 *>(modemBuf.data());
        for (spx_uint32_t i = 0; i < outLen; i++)
            out[i] = qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);
    } else {
        modemBuf = audio.data;
    }

    // Accumulate modem samples
    rxAccumulator.append(modemBuf);

    // Process complete frames
    int nin = freedv_nin(fdv);
    while (rxAccumulator.size() >= nin * (int)sizeof(qint16)) {
        const qint16 *demodIn = reinterpret_cast<const qint16 *>(rxAccumulator.constData());

        QVector<qint16> speechOut(nSpeechSamples);
        int nout = freedv_rx(fdv, speechOut.data(), const_cast<qint16 *>(demodIn));

        // Remove consumed samples
        rxAccumulator.remove(0, nin * sizeof(qint16));

        // Update nin for next iteration (changes dynamically for timing recovery)
        nin = freedv_nin(fdv);

        if (nout > 0) {
            // Resample speech rate -> radio rate if needed
            QByteArray speechData;
            if (rxUpsampler) {
                spx_uint32_t inLen = nout;
                spx_uint32_t outLen = (spx_uint32_t)(nout * (double)radioRate_ / speechSampleRate) + 64;
                QVector<float> inFloat(nout);
                for (int i = 0; i < nout; i++)
                    inFloat[i] = speechOut[i] / 32768.0f;

                QVector<float> outFloat(outLen);
                wf_resampler_process_float(rxUpsampler, 0, inFloat.data(), &inLen, outFloat.data(), &outLen);

                speechData.resize(outLen * sizeof(qint16));
                qint16 *out = reinterpret_cast<qint16 *>(speechData.data());
                for (spx_uint32_t i = 0; i < outLen; i++)
                    out[i] = qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);
            } else {
                speechData = QByteArray(reinterpret_cast<const char *>(speechOut.data()), nout * sizeof(qint16));
            }

            audioPacket out;
            out.data = speechData;
            out.time = QTime::currentTime();
            out.seq = audio.seq;
            out.sent = 0;
            out.volume = audio.volume;
            emit rxReady(out);
        }

        // Emit stats
        struct MODEM_STATS stats;
        freedv_get_modem_stats(fdv, &stats.sync, &stats.snr_est);
        emit statsUpdate(stats.snr_est, stats.sync != 0);
    }
}

void FreeDVProcessor::processTx(audioPacket audio)
{
    if (!enabled_ || !fdv) return;

    const qint16 *inSamples = reinterpret_cast<const qint16 *>(audio.data.constData());
    int inCount = audio.data.size() / (int)sizeof(qint16);

    // Resample radio rate -> speech rate if needed
    QByteArray speechBuf;
    if (txDownsampler) {
        spx_uint32_t inLen = inCount;
        spx_uint32_t outLen = (spx_uint32_t)(inCount * (double)speechSampleRate / radioRate_) + 64;
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

    // Accumulate speech samples
    txAccumulator.append(speechBuf);

    // Process complete frames
    while (txAccumulator.size() >= nSpeechSamples * (int)sizeof(qint16)) {
        const qint16 *speechIn = reinterpret_cast<const qint16 *>(txAccumulator.constData());

        QVector<qint16> modemOut(nNomModemSamples);
        freedv_tx(fdv, modemOut.data(), const_cast<qint16 *>(speechIn));

        txAccumulator.remove(0, nSpeechSamples * sizeof(qint16));

        // Resample modem rate -> radio rate if needed
        QByteArray modemData;
        if (txUpsampler) {
            spx_uint32_t inLen = nNomModemSamples;
            spx_uint32_t outLen = (spx_uint32_t)(nNomModemSamples * (double)radioRate_ / modemSampleRate) + 64;
            QVector<float> inFloat(nNomModemSamples);
            for (int i = 0; i < nNomModemSamples; i++)
                inFloat[i] = modemOut[i] / 32768.0f;

            QVector<float> outFloat(outLen);
            wf_resampler_process_float(txUpsampler, 0, inFloat.data(), &inLen, outFloat.data(), &outLen);

            modemData.resize(outLen * sizeof(qint16));
            qint16 *out = reinterpret_cast<qint16 *>(modemData.data());
            for (spx_uint32_t i = 0; i < outLen; i++)
                out[i] = qBound(-32768, (int)(outFloat[i] * 32768.0f), 32767);
        } else {
            modemData = QByteArray(reinterpret_cast<const char *>(modemOut.data()), nNomModemSamples * sizeof(qint16));
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

void FreeDVProcessor::txtRxCallback(void *state, char c)
{
    auto *self = static_cast<FreeDVProcessor *>(state);
    if (c == '\r' || c == '\n' || c == '\0') {
        QString call = self->rxTextBuffer_.trimmed().toUpper();
        if (!call.isEmpty())
            emit self->rxCallsign(call);
        self->rxTextBuffer_.clear();
    } else {
        self->rxTextBuffer_.append(c);
    }
}

char FreeDVProcessor::txtTxCallback(void * /*state*/)
{
    // No text to transmit — send space (idle)
    return ' ';
}
