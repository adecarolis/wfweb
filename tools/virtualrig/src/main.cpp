#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QList>
#include <QString>
#include <csignal>
#include <cstdio>

#include "audioconverter.h"
#include "channelmixer.h"
#include "civemulator.h"
#include "controlserver.h"
#include "virtualrig.h"
#include "extslot.h"

static QCoreApplication* g_app = nullptr;
static bool g_verbose = false;

static void sigintHandler(int)
{
    if (g_app) QMetaObject::invokeMethod(g_app, "quit", Qt::QueuedConnection);
}

// Drop QtDebugMsg unless --verbose was passed.  Mirrors wfweb's
// messageHandler so demoting a noisy log line to qDebug actually hides
// it in the testrig log instead of just changing its (invisible) tag.
// Info / warning / critical / fatal still go through.
static void virtualRigMessageHandler(QtMsgType type,
                                     const QMessageLogContext &ctx,
                                     const QString &msg)
{
    if (type == QtDebugMsg && !g_verbose) return;
    QString line = msg;
    if (ctx.category && qstrcmp(ctx.category, "default") != 0) {
        line = QString("%1: %2").arg(ctx.category, msg);
    }
    std::fputs(line.toLocal8Bit().constData(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("virtualrig");
    app.setApplicationVersion("0.1");
    g_app = &app;

    qRegisterMetaType<audioPacket>("audioPacket");
    qRegisterMetaType<rigCapabilities>("rigCapabilities");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "wfweb virtual rig simulator — presents N fake Icom rigs on localhost\n"
        "over the LAN UDP protocol and mixes audio between them.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption rigsOpt({"n", "rigs"},
        "Number of internal (LAN-UDP) virtual rigs to spawn.", "N", "2");
    QCommandLineOption externalOpt("external",
        "Number of external slots (Hamlib NET rigctl + PulseAudio bridge) "
        "for outside programs like JS8Call. 0 disables.",
        "N", "0");
    QCommandLineOption basePortOpt("base-port",
        "Control port for rig #0 (civ=+1, audio=+2; rig i uses base+i*10).",
        "port", "50001");
    QCommandLineOption rigctldBaseOpt("rigctld-base-port",
        "First port for external slots' Hamlib NET rigctl servers; slot e "
        "uses base+e (default 4532, the conventional rigctld port).",
        "port", "4532");
    QCommandLineOption attenOpt("atten",
        "Linear gain applied to inter-rig audio (default 0.1 ≈ -20 dB).",
        "gain", "0.1");
    QCommandLineOption noiseOpt("noise",
        "Per-rig noise floor RMS in Int16 units (0..1000). Default 0 "
        "(silent floor). Try ~50 for a quiet band, ~500 for a noisy one.",
        "rms", "0");
    QCommandLineOption broadcastOpt("broadcast",
        "Disable freq/mode gating; every rig hears every other rig.");
    QCommandLineOption ctrlPortOpt("control-port",
        "Port for the web control panel. 0 disables it. Default 5900.",
        "port", "5900");
    // --verbose only — no short alias because QCommandLineParser reserves
    // -v for --version.
    QCommandLineOption verboseOpt("verbose",
        "Enable qDebug output (per-frame CI-V trace, per-packet mixer trace, etc.).");

    parser.addOption(rigsOpt);
    parser.addOption(externalOpt);
    parser.addOption(basePortOpt);
    parser.addOption(rigctldBaseOpt);
    parser.addOption(attenOpt);
    parser.addOption(noiseOpt);
    parser.addOption(broadcastOpt);
    parser.addOption(ctrlPortOpt);
    parser.addOption(verboseOpt);
    parser.process(app);

    g_verbose = parser.isSet(verboseOpt);
    qInstallMessageHandler(virtualRigMessageHandler);

    bool ok = false;
    int n = parser.value(rigsOpt).toInt(&ok);
    if (!ok || n < 0 || n > 16) {
        qCritical() << "Invalid --rigs value (expected 0..16):" << parser.value(rigsOpt);
        return 2;
    }
    int nExt = parser.value(externalOpt).toInt(&ok);
    if (!ok || nExt < 0 || nExt > 16) {
        qCritical() << "Invalid --external value (expected 0..16):" << parser.value(externalOpt);
        return 2;
    }
    if (n + nExt < 1) {
        qCritical() << "Need at least one slot — pass --rigs N and/or --external N (max 16 total).";
        return 2;
    }
    if (n + nExt > 16) {
        qCritical() << "Total slots (--rigs + --external) must be <= 16:"
                    << n << "+" << nExt << "=" << (n + nExt);
        return 2;
    }
    quint16 basePort = (quint16)parser.value(basePortOpt).toUInt(&ok);
    if (!ok || basePort < 1024) {
        qCritical() << "Invalid --base-port value:" << parser.value(basePortOpt);
        return 2;
    }
    quint16 rigctldBase = (quint16)parser.value(rigctldBaseOpt).toUInt(&ok);
    if (!ok || rigctldBase < 1024) {
        qCritical() << "Invalid --rigctld-base-port value:" << parser.value(rigctldBaseOpt);
        return 2;
    }
    float atten = parser.value(attenOpt).toFloat(&ok);
    if (!ok || atten < 0.0f || atten > 4.0f) {
        qCritical() << "Invalid --atten value:" << parser.value(attenOpt);
        return 2;
    }
    float noise = parser.value(noiseOpt).toFloat(&ok);
    if (!ok || noise < 0.0f || noise > 1000.0f) {
        qCritical() << "Invalid --noise value (0..1000):" << parser.value(noiseOpt);
        return 2;
    }

    const int totalSlots = n + nExt;
    auto* mixer = new channelMixer(totalSlots, &app);
    mixer->setAttenuation(atten);
    mixer->setNoiseLevel(noise);
    mixer->setChannelRouting(!parser.isSet(broadcastOpt));

    QList<RigSlot*> slotList;
    const char* labels = "ABCDEFGHIJKLMNOP";
    for (int i = 0; i < n; ++i) {
        virtualRig::Config cfg;
        cfg.index = i;
        cfg.name = QString("virtual-IC7300-%1").arg(QChar(labels[i]));
        cfg.civAddr = 0x94;
        cfg.controlPort = basePort + i * 10;
        cfg.civPort     = basePort + i * 10 + 1;
        cfg.audioPort   = basePort + i * 10 + 2;
        auto* rig = new virtualRig(cfg, mixer, &app);
        slotList.append(rig);
        mixer->registerRig(i, rig);
        rig->start();
    }

    for (int e = 0; e < nExt; ++e) {
        const int idx = n + e;
        const QChar letter = QChar(labels[idx]);
        ExtSlot::Config cfg;
        cfg.index = idx;
        cfg.name = QString("external-%1").arg(letter);
        cfg.rigctldPort = rigctldBase + e;
        cfg.rxBusSink   = QString("virtualrig-%1-rxbus").arg(letter);
        cfg.outputSink  = QString("virtualrig-%1-output").arg(letter);
        cfg.inputSource = QString("virtualrig-%1-input").arg(letter);
        auto* extSlot = new ExtSlot(cfg, mixer, &app);
        slotList.append(extSlot);
        mixer->registerRig(idx, extSlot);
        extSlot->start();
    }

    qInfo() << "virtualrig: routing ="
            << (parser.isSet(broadcastOpt) ? "broadcast (all rigs hear all)"
                                           : "channel (same mode + passband)");

    // Web control panel — optional, same process, separate port.
    quint16 ctrlPort = (quint16)parser.value(ctrlPortOpt).toUInt(&ok);
    if (ok && ctrlPort != 0) {
        auto* ctrl = new controlServer(mixer, &app);
        if (!ctrl->listen(ctrlPort)) {
            qWarning() << "controlServer disabled (port" << ctrlPort << "unavailable).";
        }
    }

    std::signal(SIGINT, sigintHandler);
    std::signal(SIGTERM, sigintHandler);

    qInfo() << "virtualrig: ready.  Ctrl-C to stop.";
    int rc = app.exec();

    for (auto* r : slotList) r->stop();
    qDeleteAll(slotList);
    return rc;
}
