#include "virtualrig.h"
#include "channelmixer.h"
#include "civemulator.h"

#include "icomserver.h"
#include "rigcommander.h"
#include "wfwebtypes.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QDateTime>
#include <cmath>
#include <random>

namespace {
// Channel bandwidth modeled as TX low-pass cutoff (Hz). The mixer already
// gates by mode at the channel level; this complements that by limiting
// what audio spectrum each transmitter actually emits — so a USB rig
// can't dump 10 kHz of bandwidth onto the bus just because the mic
// captured it.
float txCutoffForMode(quint8 mode)
{
    switch (mode) {
    case 0x00: case 0x01: return 3500.0f;   // LSB/USB
    case 0x02:            return 5000.0f;   // AM
    case 0x03: case 0x07: return 500.0f;    // CW / CW-R
    case 0x04: case 0x08: return 500.0f;    // RTTY / RTTY-R
    case 0x05:            return 12000.0f;  // FM voice
    case 0x17:            return 5000.0f;   // DV
    default:              return 3500.0f;
    }
}
} // namespace

void virtualRig::BiquadLpf::tune(float fs, float fc)
{
    // Audio EQ Cookbook LPF biquad, Butterworth (Q=1/sqrt(2)).
    const float Q = 0.7071067811865475f;
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float cos_w0 = std::cos(w0);
    float sin_w0 = std::sin(w0);
    float alpha = sin_w0 / (2.0f * Q);
    float a0 = 1.0f + alpha;
    b0 = (1.0f - cos_w0) * 0.5f / a0;
    b1 = (1.0f - cos_w0) / a0;
    b2 = b0;
    a1 = -2.0f * cos_w0 / a0;
    a2 = (1.0f - alpha) / a0;
    // Don't reset history — small discontinuity is less ugly than a sudden
    // zeroing click when the user changes mode mid-stream.
}

float virtualRig::BiquadLpf::process(float x)
{
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
}

virtualRig::virtualRig(const Config& cfg, channelMixer* mixer, QObject* parent)
    : QObject(parent), cfg(cfg), mixer(mixer)
{
    // --- SERVERCONFIG: single-rig, LAN-mode. ---
    serverCfg.enabled = true;
    serverCfg.disableUI = true;
    serverCfg.lan = true;
    serverCfg.controlPort = cfg.controlPort;
    serverCfg.civPort = cfg.civPort;
    serverCfg.audioPort = cfg.audioPort;
    serverCfg.audioOutput = 0;
    serverCfg.audioInput = 0;
    serverCfg.resampleQuality = 0;
    serverCfg.baudRate = 115200;

    // Fixed credentials so the client's login is accepted. icomServer rejects
    // any user config with empty username/password (icomserver.cpp:307), so we
    // can't be fully permissive — the orchestrator just has to send the same.
    SERVERUSER u;
    u.username = "wfweb";
    u.password = "wfweb";
    u.userType = 0;
    serverCfg.users.append(u);

    // --- RIGCONFIG: stable per-rig identity derived from the name. ---
    rigCfg.serialPort = "";
    rigCfg.baudRate = 115200;
    rigCfg.civAddr = cfg.civAddr;
    rigCfg.civIsRadioModel = true;
    rigCfg.hasWiFi = false;
    rigCfg.hasEthernet = true;
    rigCfg.pttType = pttCIV;
    rigCfg.modelName = "Virtual IC-7300";
    rigCfg.rigName = cfg.name;
    rigCfg.rigAvailable = true;
    rigCfg.rig = nullptr;      // ← the gap that the icomserver null-guards handle
    rigCfg.rigThread = nullptr;
    rigCfg.rxaudio = nullptr;
    rigCfg.txaudio = nullptr;
    rigCfg.queueInterval = 1000;

    // Deterministic GUID from the rig name so clients remember us.
    QByteArray h = QCryptographicHash::hash(cfg.name.toUtf8(), QCryptographicHash::Md5);
    memcpy(rigCfg.guid, h.constData(), GUIDLEN);

    serverCfg.rigs.append(&rigCfg);

    // --- Synthetic rigCapabilities (minimum fields actually read) ---
    caps.model = cfg.civAddr;
    caps.modelID = cfg.civAddr;
    caps.manufacturer = manufIcom;
    caps.modelName = rigCfg.modelName;
    caps.hasLan = true;
    caps.hasEthernet = true;
    caps.hasTransmit = true;
    caps.hasPTTCommand = true;
    caps.baudRate = 115200;
    memcpy(caps.guid, rigCfg.guid, GUIDLEN);

    civ = new civEmulator(cfg.civAddr, this);
    civ->setName(cfg.name);
}

virtualRig::~virtualRig()
{
    stop();
}

quint64 virtualRig::freq() const { return civ ? civ->frequency() : 0ULL; }
quint8  virtualRig::mode() const { return civ ? civ->rigMode()   : 0x01; }

void virtualRig::start()
{
    if (server != nullptr) return;

    serverThread = new QThread(this);
    serverThread->setObjectName(QString("icomServer-%1").arg(cfg.name));

    server = new icomServer(&serverCfg);
    server->moveToThread(serverThread);

    // Kick init() once the thread starts.
    QObject::connect(serverThread, &QThread::started, server, &rigServer::init);
    QObject::connect(serverThread, &QThread::finished, server, &QObject::deleteLater);

    // CI-V: client → emulator → back to client.
    QObject::connect(server, &rigServer::haveDataFromServer,
                     civ, &civEmulator::onCivFromClient,
                     Qt::QueuedConnection);
    QObject::connect(civ, &civEmulator::replyFrame,
                     server, &rigServer::dataForServer,
                     Qt::QueuedConnection);
    QObject::connect(civ, &civEmulator::pttChanged,
                     this, &virtualRig::onPttChanged);

    // Audio: TX from client → our gate → mixer; RX from mixer → server.
    QObject::connect(server, &rigServer::haveAudioData,
                     this, &virtualRig::onTxAudioFromClient,
                     Qt::QueuedConnection);
    QObject::connect(mixer, &channelMixer::rxAudioForRig,
                     this, &virtualRig::onRxAudioFromMixer,
                     Qt::QueuedConnection);

    serverThread->start();

    // Deliver synthetic caps to the server once it's up. Queued so it runs
    // after init() on the server thread.
    QMetaObject::invokeMethod(server, [this]() {
        server->receiveRigCaps(&caps);
    }, Qt::QueuedConnection);

    // Continuous silence on RX. The wfweb client's icomUdpAudio watchdog
    // kills the TX audio handler if it sees no RX for 30 s, so real silence
    // (not absence of packets) is what keeps TX alive when nobody is keyed.
    idleRxTimer = new QTimer(this);
    idleRxTimer->setInterval(20); // 20 ms cadence
    connect(idleRxTimer, &QTimer::timeout, this, &virtualRig::emitIdleRx);
    idleRxTimer->start();

    qInfo() << "virtualRig" << cfg.name << "up on ports"
            << cfg.controlPort << cfg.civPort << cfg.audioPort;
}

void virtualRig::stop()
{
    if (idleRxTimer != nullptr) {
        idleRxTimer->stop();
        idleRxTimer = nullptr; // parented to this, deleted by Qt
    }
    if (serverThread != nullptr) {
        serverThread->quit();
        serverThread->wait(2000);
        serverThread = nullptr;
        server = nullptr; // deleted via finished()
    }
}

void virtualRig::emitIdleRx()
{
    if (server == nullptr || ptt) return;
    // One 20 ms chunk of LPCM mono @ 48 kHz = 960 samples * 2 bytes = 1920 B.
    // Matches the codec negotiated by the client (rx sample rate 48000, codec
    // 4 = LPCM 16-bit uncompressed). Always emit exactly this size on every
    // tick — pulling from the real-audio buffer first, padding with silence
    // at the tail if short. Strict cadence + size keeps the client's jitter
    // buffer happy and prevents interleaving with silence mid-speech.
    static const int bytes = 48 * 20 * 2;
    QByteArray data(bytes, '\0');
    qint16 peak = 0;
    {
        QMutexLocker lock(&rxMutex);
        int take = qMin(rxBuffer.size(), bytes);
        if (take > 0) {
            memcpy(data.data(), rxBuffer.constData(), take);
            rxBuffer.remove(0, take);
            // Cap runaway growth if the timer ever stalls; drop oldest.
            const int maxBytes = 48 * 500 * 2; // 500 ms
            if (rxBuffer.size() > maxBytes) {
                rxBuffer.remove(0, rxBuffer.size() - maxBytes);
            }
        }
        // Peak over the portion we actually filled with real audio.
        const qint16* s = reinterpret_cast<const qint16*>(data.constData());
        int n = take / 2;
        for (int i = 0; i < n; ++i) {
            qint16 v = s[i] < 0 ? (qint16)-s[i] : s[i];
            if (v > peak) peak = v;
        }
    }
    // Additive white Gaussian noise at the rig's configured floor. Makes
    // the noise floor audible when nothing is being received, and gives
    // RADE/FreeDV robustness testing a real SNR target to hit.
    float nRms = mixer ? mixer->noiseLevel(cfg.index) : 0.0f;
    if (nRms > 0.0f) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<float> dist(0.0f, nRms);
        qint16* samples = reinterpret_cast<qint16*>(data.data());
        const int n = data.size() / (int)sizeof(qint16);
        qint16 noisyPeak = peak;
        for (int i = 0; i < n; ++i) {
            int v = samples[i] + (int)dist(rng);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            samples[i] = (qint16)v;
            qint16 a = v < 0 ? (qint16)-v : (qint16)v;
            if (a > noisyPeak) noisyPeak = a;
        }
        peak = noisyPeak;
    }

    if (civ) civ->setSMeterFromPeak((quint16)peak);
    audioPacket pkt;
    pkt.data = data;
    pkt.seq = rxSeq++;
    pkt.time = QTime::currentTime();
    pkt.sent = 0;
    pkt.amplitudePeak = peak;
    pkt.amplitudeRMS = 0;
    pkt.volume = 0;
    QMetaObject::invokeMethod(server, [this, pkt]() {
        server->receiveAudioData(pkt);
    }, Qt::QueuedConnection);
}

void virtualRig::onPttChanged(bool on)
{
    qInfo() << "virtualRig" << cfg.name << "PTT" << (on ? "ON" : "OFF");
    ptt = on;
}

void virtualRig::onTxAudioFromClient(const audioPacket& pkt)
{
    // Always compute peak so the TX meter can reflect recent mic level whenever
    // PTT transitions on, even for the first packet.
    const qint16* s = reinterpret_cast<const qint16*>(pkt.data.constData());
    int n = pkt.data.size() / 2;
    qint16 peak = 0;
    for (int i = 0; i < n; ++i) {
        qint16 v = s[i] < 0 ? (qint16)-s[i] : s[i];
        if (v > peak) peak = v;
    }
    if (civ) civ->setTxMeterFromPeak((quint16)peak);
    if (!ptt) return;
    if (!mixer) return;

    // Band-limit to the current mode's TX bandwidth. Client audio is LPCM
    // mono at the negotiated rx rate (48 kHz — see servermain.cpp:542),
    // which is also what the browser captures, so fs=48000 is safe here.
    quint8 mode = civ ? civ->rigMode() : 0x01;
    if (mode != txLpf.tunedForMode) {
        txLpf.tune(48000.0f, txCutoffForMode(mode));
        txLpf.tunedForMode = mode;
    }
    audioPacket filtered = pkt;
    filtered.data.resize(pkt.data.size());
    qint16* out = reinterpret_cast<qint16*>(filtered.data.data());
    for (int i = 0; i < n; ++i) {
        float y = txLpf.process((float)s[i]);
        int v = (int)y;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[i] = (qint16)v;
    }
    mixer->pushTxAudio(cfg.index, filtered);
}

void virtualRig::onRxAudioFromMixer(int dstRig, const audioPacket& pkt)
{
    if (dstRig != cfg.index) return;
    static int tick = 0;
    if (++tick % 50 == 1) {
        qInfo() << "virtualRig" << cfg.name << "RX from mixer: bytes=" << pkt.data.size();
    }
    // Buffer the real PCM and let the 20 ms tick drain it at a fixed cadence.
    // S-meter is recomputed from whatever bytes are actually emitted, so it
    // reflects what the client will hear, not speculative buffer contents.
    QMutexLocker lock(&rxMutex);
    rxBuffer.append(pkt.data);
    const int maxBytes = 48 * 500 * 2; // 500 ms hard cap
    if (rxBuffer.size() > maxBytes) {
        rxBuffer.remove(0, rxBuffer.size() - maxBytes);
    }
}
