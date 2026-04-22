#ifndef CHANNELMIXER_H
#define CHANNELMIXER_H

#include <QObject>
#include <QMutex>
#include <QString>
#include <QVector>

#include "audioconverter.h"

class virtualRig;

// Audio bus with channel-aware routing. TX from rig i is forwarded to rig j
// only when they share a mode and are within receiver-passband frequency
// tolerance — a simple simulation of "can you hear me on this channel?".
// Falls back to pure broadcast when freq/mode gating is disabled. Assumes
// all rigs share the same codec / sample rate (negotiated by icomServer
// at connect time).
class channelMixer : public QObject
{
    Q_OBJECT

public:
    // Ham band buckets used by the per-band attenuation matrix. "Other"
    // catches anything outside the listed ranges so we don't lose traffic.
    enum Band {
        Band160m = 0, Band80m, Band60m, Band40m, Band30m, Band20m,
        Band17m, Band15m, Band12m, Band10m, Band6m, Band2m, Band70cm,
        BandOther,
        BandCount
    };
    static Band bandForFreq(quint64 hz);
    static QString bandName(Band b);

    explicit channelMixer(int numRigs, QObject* parent = nullptr);

    // Set uniform linear gain across every src→dst link and every band.
    // Default ~0.1 (≈ -20 dB). Convenient for quick CLI setup.
    void setAttenuation(float gain);

    // Per-link, per-band gain. "Band" reflects the SOURCE rig's current
    // band — i.e., what's being transmitted, not what the destination is
    // tuned to. Gains are kept symmetric: setLinkAttenuation(a,b,…) also
    // updates (b,a,…) so the path between two rigs has a single value
    // regardless of who keys up.
    void setLinkAttenuation(int src, int dst, Band band, float gain);
    float linkAttenuation(int src, int dst, Band band) const;

    // Per-destination-rig noise floor, in Int16 RMS units (0..32767).
    // White Gaussian noise at this RMS is added to every chunk the rig emits
    // to its client — so the noise floor is always present, signal or not.
    void setNoiseLevel(float rms);                  // all rigs
    void setNoiseLevel(int dstRig, float rms);      // one rig
    float noiseLevel(int dstRig) const;

    // Disable channel gating (everyone hears everyone). Default: enabled.
    void setChannelRouting(bool on);
    bool channelRoutingEnabled() const;

    // Register a rig so the mixer can read its current freq/mode on forward.
    // Caller retains ownership; must outlive the mixer.
    void registerRig(int idx, virtualRig* rig);

    int rigCount() const { return numRigs; }
    virtualRig* rigAt(int i) const;

public slots:
    void pushTxAudio(int srcRig, const audioPacket& pkt);

signals:
    // Emitted once per destination rig, per incoming TX packet.
    // Connect with a queued connection so it lands in the destination
    // icomServer's thread.
    void rxAudioForRig(int dstRig, const audioPacket& pkt);

private:
    int numRigs;
    bool channelRouting;
    // linkGainByBand[src][dst][band] — directed gain per pair, per band.
    // Diagonal (src==dst) is unused.
    QVector<QVector<QVector<float>>> linkGainByBand;
    // noiseRms[rig] — receive-side noise added on emit.
    QVector<float> noiseRms;
    QVector<virtualRig*> rigs;
    mutable QMutex mx;
};

#endif
