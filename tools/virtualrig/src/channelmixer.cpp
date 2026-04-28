#include "channelmixer.h"
#include "virtualrig.h"

#include <QDebug>
#include <QtEndian>
#include <cstring>

namespace {
// Receiver passband tolerance per Icom mode code. 0x00=LSB 0x01=USB 0x02=AM
// 0x03=CW 0x04=RTTY 0x05=FM 0x07=CW-R 0x08=RTTY-R 0x17=DV. Rough but
// good enough for a test bench — models "signal lands in my receiver's
// passband" rather than any real propagation physics.
qint64 passbandHz(quint8 mode)
{
    switch (mode) {
    case 0x00: case 0x01: return 3000;   // SSB: ~2.7 kHz + slack
    case 0x02:            return 5000;   // AM (double-sideband)
    case 0x03: case 0x07: return 500;    // CW: narrow
    case 0x04: case 0x08: return 500;    // RTTY: narrow
    case 0x05:            return 10000;  // FM
    case 0x17:            return 5000;   // DV
    default:              return 3000;
    }
}

bool channelCompatible(quint64 srcFreq, quint8 srcMode,
                       quint64 dstFreq, quint8 dstMode)
{
    // Strict mode match. USB↔LSB etc. would require sideband-inversion
    // simulation we don't do; keep it simple.
    if (srcMode != dstMode) return false;
    qint64 dHz = (qint64)srcFreq - (qint64)dstFreq;
    if (dHz < 0) dHz = -dHz;
    return dHz <= passbandHz(srcMode);
}
} // namespace

channelMixer::Band channelMixer::bandForFreq(quint64 hz)
{
    // IARU Region 1 amateur allocations, slightly widened at the edges
    // so a rig parked at e.g. 14.349 MHz still counts as "20 m".
    if (hz >=   1800000ULL && hz <=   2000000ULL) return Band160m;
    if (hz >=   3500000ULL && hz <=   4000000ULL) return Band80m;
    if (hz >=   5250000ULL && hz <=   5450000ULL) return Band60m;
    if (hz >=   7000000ULL && hz <=   7300000ULL) return Band40m;
    if (hz >=  10100000ULL && hz <=  10150000ULL) return Band30m;
    if (hz >=  14000000ULL && hz <=  14350000ULL) return Band20m;
    if (hz >=  18068000ULL && hz <=  18168000ULL) return Band17m;
    if (hz >=  21000000ULL && hz <=  21450000ULL) return Band15m;
    if (hz >=  24890000ULL && hz <=  24990000ULL) return Band12m;
    if (hz >=  28000000ULL && hz <=  29700000ULL) return Band10m;
    if (hz >=  50000000ULL && hz <=  54000000ULL) return Band6m;
    if (hz >= 144000000ULL && hz <= 148000000ULL) return Band2m;
    if (hz >= 430000000ULL && hz <= 450000000ULL) return Band70cm;
    return BandOther;
}

QString channelMixer::bandName(Band b)
{
    switch (b) {
    case Band160m: return "160m";
    case Band80m:  return "80m";
    case Band60m:  return "60m";
    case Band40m:  return "40m";
    case Band30m:  return "30m";
    case Band20m:  return "20m";
    case Band17m:  return "17m";
    case Band15m:  return "15m";
    case Band12m:  return "12m";
    case Band10m:  return "10m";
    case Band6m:   return "6m";
    case Band2m:   return "2m";
    case Band70cm: return "70cm";
    case BandOther:
    default:       return "other";
    }
}

channelMixer::channelMixer(int numRigs, QObject* parent)
    : QObject(parent), numRigs(numRigs), channelRouting(true)
{
    rigs.resize(numRigs);
    noiseRms = QVector<float>(numRigs, 0.0f);
    linkGainByBand.resize(numRigs);
    for (auto& row : linkGainByBand) {
        row.resize(numRigs);
        for (auto& cell : row) cell = QVector<float>(BandCount, 0.1f);
    }
}

void channelMixer::setAttenuation(float gain)
{
    QMutexLocker lock(&mx);
    for (auto& row : linkGainByBand)
        for (auto& cell : row)
            for (auto& g : cell) g = gain;
}

void channelMixer::setLinkAttenuation(int src, int dst, Band band, float gain)
{
    QMutexLocker lock(&mx);
    if (src < 0 || src >= linkGainByBand.size()) return;
    if (dst < 0 || dst >= linkGainByBand[src].size()) return;
    if (band < 0 || band >= BandCount) return;
    // Links are symmetric — same path loss either way.
    linkGainByBand[src][dst][band] = gain;
    linkGainByBand[dst][src][band] = gain;
}

float channelMixer::linkAttenuation(int src, int dst, Band band) const
{
    QMutexLocker lock(&mx);
    if (src < 0 || src >= linkGainByBand.size()) return 0.0f;
    if (dst < 0 || dst >= linkGainByBand[src].size()) return 0.0f;
    if (band < 0 || band >= BandCount) return 0.0f;
    return linkGainByBand[src][dst][band];
}

void channelMixer::setNoiseLevel(float rms)
{
    QMutexLocker lock(&mx);
    for (auto& v : noiseRms) v = rms;
}

void channelMixer::setNoiseLevel(int dstRig, float rms)
{
    QMutexLocker lock(&mx);
    if (dstRig >= 0 && dstRig < noiseRms.size()) noiseRms[dstRig] = rms;
}

float channelMixer::noiseLevel(int dstRig) const
{
    QMutexLocker lock(&mx);
    if (dstRig >= 0 && dstRig < noiseRms.size()) return noiseRms[dstRig];
    return 0.0f;
}

void channelMixer::setChannelRouting(bool on)
{
    QMutexLocker lock(&mx);
    channelRouting = on;
}

bool channelMixer::channelRoutingEnabled() const
{
    QMutexLocker lock(&mx);
    return channelRouting;
}

void channelMixer::registerRig(int idx, virtualRig* rig)
{
    QMutexLocker lock(&mx);
    if (idx >= 0 && idx < rigs.size()) rigs[idx] = rig;
}

virtualRig* channelMixer::rigAt(int i) const
{
    QMutexLocker lock(&mx);
    if (i < 0 || i >= rigs.size()) return nullptr;
    return rigs[i];
}

void channelMixer::pushTxAudio(int srcRig, const audioPacket& pkt)
{
    int n;
    bool gate;
    QVector<virtualRig*> snap;
    QVector<float> gains;       // per-destination gain at the source's band
    {
        QMutexLocker lock(&mx);
        n = numRigs;
        gate = channelRouting;
        snap = rigs;
        gains.resize(n);
        Band srcBand = BandOther;
        if (srcRig >= 0 && srcRig < snap.size() && snap[srcRig]) {
            srcBand = bandForFreq(snap[srcRig]->freq());
        }
        for (int dst = 0; dst < n; ++dst) {
            if (dst == srcRig) { gains[dst] = 0.0f; continue; }
            if (srcRig >= 0 && srcRig < linkGainByBand.size() &&
                dst < linkGainByBand[srcRig].size()) {
                gains[dst] = linkGainByBand[srcRig][dst][srcBand];
            } else {
                gains[dst] = 0.0f;
            }
        }
    }

    const int sampleCount = pkt.data.size() / (int)sizeof(qint16);
    const qint16* srcSamples = reinterpret_cast<const qint16*>(pkt.data.constData());

    static int tick = 0;
    if (++tick % 50 == 1) {
        qDebug() << "mixer: tx from rig" << srcRig << "samples=" << sampleCount
                 << "n=" << n;
    }

    quint64 srcFreq = 0;
    quint8 srcMode = 0;
    if (gate && srcRig >= 0 && srcRig < snap.size() && snap[srcRig]) {
        srcFreq = snap[srcRig]->freq();
        srcMode = snap[srcRig]->mode();
    }

    for (int dst = 0; dst < n; ++dst) {
        if (dst == srcRig) continue;
        if (gate && dst < snap.size() && snap[dst]) {
            if (!channelCompatible(srcFreq, srcMode,
                                   snap[dst]->freq(), snap[dst]->mode())) {
                continue;
            }
        }

        // Per-link scaling: build a fresh PCM copy so each destination can
        // see a different fade without interfering with the others.
        float g = gains[dst];
        audioPacket out = pkt;
        out.data.resize(pkt.data.size());
        qint16* dstSamples = reinterpret_cast<qint16*>(out.data.data());
        if (g == 1.0f) {
            memcpy(out.data.data(), pkt.data.constData(), pkt.data.size());
        } else {
            for (int i = 0; i < sampleCount; ++i) {
                int v = static_cast<int>(srcSamples[i] * g);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                dstSamples[i] = static_cast<qint16>(v);
            }
        }
        emit rxAudioForRig(dst, out);
    }
}
