#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QList>
#include <csignal>

#include "audioconverter.h"
#include "channelmixer.h"
#include "civemulator.h"
#include "controlserver.h"
#include "virtualrig.h"

static QCoreApplication* g_app = nullptr;

static void sigintHandler(int)
{
    if (g_app) QMetaObject::invokeMethod(g_app, "quit", Qt::QueuedConnection);
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
        "Number of virtual rigs to spawn.", "N", "2");
    QCommandLineOption basePortOpt("base-port",
        "Control port for rig #0 (civ=+1, audio=+2; rig i uses base+i*10).",
        "port", "50001");
    QCommandLineOption attenOpt("atten",
        "Linear gain applied to inter-rig audio (default 0.1 ≈ -20 dB).",
        "gain", "0.1");
    QCommandLineOption noiseOpt("noise",
        "Per-rig noise floor RMS in Int16 units (0..32767). Default 0 "
        "(silent floor). Try ~50 for a quiet band, ~500 for a noisy one.",
        "rms", "0");
    QCommandLineOption broadcastOpt("broadcast",
        "Disable freq/mode gating; every rig hears every other rig.");
    QCommandLineOption ctrlPortOpt("control-port",
        "Port for the web control panel. 0 disables it. Default 5900.",
        "port", "5900");

    parser.addOption(rigsOpt);
    parser.addOption(basePortOpt);
    parser.addOption(attenOpt);
    parser.addOption(noiseOpt);
    parser.addOption(broadcastOpt);
    parser.addOption(ctrlPortOpt);
    parser.process(app);

    bool ok = false;
    int n = parser.value(rigsOpt).toInt(&ok);
    if (!ok || n < 1 || n > 16) {
        qCritical() << "Invalid --rigs value (expected 1..16):" << parser.value(rigsOpt);
        return 2;
    }
    quint16 basePort = (quint16)parser.value(basePortOpt).toUInt(&ok);
    if (!ok || basePort < 1024) {
        qCritical() << "Invalid --base-port value:" << parser.value(basePortOpt);
        return 2;
    }
    float atten = parser.value(attenOpt).toFloat(&ok);
    if (!ok || atten < 0.0f || atten > 4.0f) {
        qCritical() << "Invalid --atten value:" << parser.value(attenOpt);
        return 2;
    }
    float noise = parser.value(noiseOpt).toFloat(&ok);
    if (!ok || noise < 0.0f || noise > 32767.0f) {
        qCritical() << "Invalid --noise value:" << parser.value(noiseOpt);
        return 2;
    }

    auto* mixer = new channelMixer(n, &app);
    mixer->setAttenuation(atten);
    mixer->setNoiseLevel(noise);
    mixer->setChannelRouting(!parser.isSet(broadcastOpt));

    QList<virtualRig*> rigs;
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
        rigs.append(rig);
        mixer->registerRig(i, rig);
        rig->start();
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

    for (auto* r : rigs) r->stop();
    qDeleteAll(rigs);
    return rc;
}
