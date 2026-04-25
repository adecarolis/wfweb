#ifdef BUILD_WFSERVER
#include <QtCore/QCoreApplication>
#include "keyboard.h"
#else
#include <QApplication>
#include <QTranslator>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <csignal>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <iostream>

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#include <QAudioDeviceInfo>
#else
#include <QMediaDevices>
#include <QAudioDevice>
#endif

#ifdef BUILD_WFSERVER
#include "servermain.h"
#else
#include "wfmain.h"
#endif

#include "logcategories.h"

#include "direwolfprocessor.h"

bool debugMode=false;

#ifdef BUILD_WFSERVER
// Smart pointer to log file
QScopedPointer<QFile>   m_logFile;
QMutex logMutex;
servermain* w=Q_NULLPTR;
keyboard* kb=Q_NULLPTR;

#ifdef Q_OS_WIN
bool __stdcall cleanup(DWORD sig)
 #else
static void cleanup(int sig)
 #endif
{
    switch(sig) {
#ifndef Q_OS_WIN
    case SIGHUP:
        qInfo() << "hangup signal";
        break;
#endif
    case SIGINT:
    case SIGTERM:
        qInfo() << "terminate signal caught";
        if (kb!=Q_NULLPTR) kb->terminate();
        if (w!=Q_NULLPTR) w->deleteLater();
        QCoreApplication::quit();
        break;
    default:
        break;
    }

 #ifdef Q_OS_WIN
    return true;
 #else
    return;
 #endif
}


 #ifndef Q_OS_WIN
void initDaemon()
{
    int i;
    if(getppid()==1)
        return; /* already a daemon */
    i=fork();
    if (i<0)
        exit(1); /* fork error */
    if (i>0)
        exit(0); /* parent exits */

    setsid(); /* obtain a new process group */

    for (i=getdtablesize();i>=0;--i)
        close(i); /* close all descriptors */
    i=open("/dev/null",O_RDWR); dup(i); dup(i);

    signal(SIGCHLD,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
}

 #else

void initDaemon() {
    std::cout << "Background mode does not currently work in Windows\n";
    exit(1);
}

 #endif

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
#endif

int main(int argc, char *argv[])
{
#ifndef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif

#ifdef BUILD_WFSERVER
#ifdef Q_OS_WIN
    // QCoreApplication doesn't load the Windows platform plugin, so COM is
    // never initialised.  WASAPI audio capture requires COM on every thread
    // that touches audio objects.  Initialise it here for the main thread
    // (device enumeration) — the web-server thread does its own init.
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif
    QCoreApplication a(argc, argv);
    a.setOrganizationName("wfweb");
    a.setOrganizationDomain("wfweb.org");
    a.setApplicationName("wfweb");
    kb = new keyboard();
#else
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QApplication a(argc, argv);
    a.setOrganizationName("wfweb");
    a.setOrganizationDomain("wfweb.org");
    a.setApplicationName("wfweb");
#endif

#ifdef QT_DEBUG
    //debugMode = true;
#endif

    QDateTime date = QDateTime::currentDateTime();
    QString formattedTime = date.toString("dd.MM.yyyy hh:mm:ss");
    QString logFilename = (QString("%1/%2-%3.log").arg(QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0]).arg(a.applicationName()).arg(date.toString("yyyyMMddhhmmss")));

    QString settingsFile = NULL;
    QString currentArg;
    cmdLineOverrides overrides;


    const QString helpText = QString(
        "\nUsage: wfweb [options]\n"
        "  -s --settings <file>    Settings .ini file\n"
        "  -p --port <port>        Web server port\n"
        "  -S --no-web             Disable web server, enable rig server\n"
        "  --lan <ip>              Connect via LAN to IP (enables LAN mode)\n"
        "  --lan-control <port>    LAN control port (default 50001)\n"
        "  --lan-serial <port>     LAN serial port (default 50002)\n"
        "  --lan-audio <port>      LAN audio port (default 50003)\n"
        "  --lan-user <user>       LAN username\n"
        "  --lan-pass <pass>       LAN password\n"
        "  --civ <addr>            CI-V address, hex or decimal (e.g. 0x94 or 148 for IC-7300)\n"
        "  --serial-port <path>    Serial port device (e.g. /dev/ttyUSB0)\n"
        "  --audio-system <id>     Audio system (0=Qt, 1=PortAudio, 2=RtAudio)\n"
        "  --audio-device <name>   Audio device name (e.g. 'USB Audio CODEC')\n"
        "                          Use '--audio-device list' to show available devices\n"
        "  --manufacturer <id>     Manufacturer ID (0=Icom, 1=Kenwood, 2=Yaesu)\n"
        "  -l --logfile <file>     Log file\n"
        "  -b --background         Run as daemon (not Windows)\n"
        "  -d --debug [file]       Enable verbose debug logging (optionally to file)\n"
        "  -c --clearconfig CONFIRM  Clear all settings\n"
        "  --packet-self-test      Run Dire Wolf encode/decode loopback and exit\n"
        "  -v --version            Show version\n"
        "  -? --help               Show this help\n");
#ifdef BUILD_WFSERVER
    const QString version = QString("wfweb version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFWEB_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());
#else
    const QString version = QString("wfweb version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFWEB_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());

    // Translator doesn't really make sense for wfweb right now.
    QTranslator myappTranslator;
    qDebug() << "Current translation language: " << myappTranslator.language();

    bool trResult = myappTranslator.load(QLocale(), QLatin1String("wfweb"), QLatin1String("_"), QLatin1String(":/translations"));
    if(trResult) {
        qDebug() << "Recognized requested language and loaded the translations (or at least found the /translations resource folder). Installing translator.";
        a.installTranslator(&myappTranslator);
    } else {
        qDebug() << "Could not load translation.";
    }

    qDebug() << "Changed to translation language: " << myappTranslator.language();
#endif

    // Early CLI short-circuit: run the packet self-test and exit, without
    // starting the web server or touching radio state.  Used by
    // tests/test_packet.py.
    for (int c = 1; c < argc; c++) {
        if (QString(argv[c]) == "--packet-self-test") {
            return DireWolfProcessor::runSelfTest();
        }
    }

    for(int c=1; c<argc; c++)
    {
        //qInfo() << "Argc: " << c << " argument: " << argv[c];
        currentArg = QString(argv[c]);

        if ((currentArg == "-d") || (currentArg == "--debug"))
        {
            debugMode = true;
            // Optional filename argument: --debug [file]
            if (argc > c + 1 && argv[c + 1][0] != '-')
            {
                logFilename = argv[c + 1];
                c += 1;
            }
        }
        else if ((currentArg == "-l") || (currentArg == "--logfile"))
        {
            if (argc > c)
            {
                logFilename = argv[c + 1];
                c += 1;
            }
        }
        else if ((currentArg == "-s") || (currentArg == "--settings"))
        {
            if (argc > c)
            {
                settingsFile = argv[c + 1];
                c += 1;
                if (settingsFile.endsWith(".rig", Qt::CaseInsensitive))
                {
                    std::cout <<
                        "Error: '" << settingsFile.toStdString() << "' is a radio\n"
                        "definition file (.rig), not a wfweb settings file. --settings\n"
                        "does not take .rig files.\n"
                        "\n"
                        ".rig files are CI-V command dictionaries for specific radio\n"
                        "models. wfweb already loads them automatically from its install's\n"
                        "rigs/ directory based on the radio it detects on the CI-V bus.\n"
                        "You should never pass one on the command line.\n"
                        "\n"
                        "What --settings is actually for:\n"
                        "\n"
                        "wfweb normally stores YOUR preferences (which serial port, which\n"
                        "audio device, web-server port, LAN address and credentials, per-\n"
                        "radio config, etc.) in the OS default location under your user\n"
                        "profile. --settings lets you point it somewhere else instead.\n"
                        "The file itself is created and managed by wfweb — you don't\n"
                        "hand-write it; you edit settings through the web UI and wfweb\n"
                        "persists them to whichever file you named.\n"
                        "\n"
                        "Common reasons to use --settings:\n"
                        "\n"
                        "  * Running multiple wfweb instances in parallel, one per radio.\n"
                        "    Give each its own --settings file so they don't overwrite\n"
                        "    each other's config.\n"
                        "\n"
                        "  * Docker or systemd deployments where the default per-user\n"
                        "    location is inconvenient. Mount or install a config at a\n"
                        "    fixed, predictable path (e.g. /etc/wfweb/station.ini).\n"
                        "\n"
                        "  * Named profiles you can switch between or back up:\n"
                        "    home.ini, contest.ini, portable.ini, etc.\n"
                        "\n"
                        "To create a fresh settings file, just run:\n"
                        "\n"
                        "    wfweb -s ./my-profile.ini\n"
                        "\n"
                        "wfweb writes a file with sensible defaults on first run. After\n"
                        "that, open the web UI and configure as usual — your changes are\n"
                        "saved back to that file.\n"
                        "\n"
                        "If all you need is to talk to a rig on a non-default CI-V address\n"
                        "or a different manufacturer, you don't need --settings at all —\n"
                        "use --civ <addr> and --manufacturer <id> directly.\n";
                    return -1;
                }
            }
        }
        else if ((currentArg == "-c") || (currentArg == "--clearconfig"))
        {
            if (argc > c)
            {
                QString confirm = argv[c + 1];
                c += 1;
                if (confirm == "CONFIRM") {
                    QSettings* settings;
                    // Clear config
                    if (settingsFile.isEmpty()) {
                        settings = new QSettings();
                    }
                    else
                    {
                        QString file = settingsFile;
                        QFile info(settingsFile);
                        QString path="";
                        if (!QFileInfo(info).isAbsolute())
                        {
                            path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                            if (path.isEmpty())
                            {
                                path = QDir::homePath();
                            }
                            path = path + "/";
                            file = info.fileName();
                        }
                        settings = new QSettings(path + file, QSettings::Format::IniFormat);
                    }
                    settings->clear();
                    delete settings;
                    std::cout << QString("All wfweb settings cleared.\n").toStdString();
                    exit(0);
                }
            }
            std::cout << QString("Error: Clear config not confirmed (please add the word CONFIRM), aborting\n").toStdString();
            std::cout << helpText.toStdString();
            exit(-1);
        }
#ifdef BUILD_WFSERVER
        else if ((currentArg == "-b") || (currentArg == "--background"))
        {
            initDaemon();
        }
#endif
        else if ((currentArg == "-p") || (currentArg == "--port"))
        {
            if (argc > c + 1)
            {
                bool ok;
                int port = QString(argv[c + 1]).toInt(&ok);
                if (ok && port > 0 && port <= 65535)
                {
                    overrides.webPort = static_cast<quint16>(port);
                    c += 1;
                }
                else
                {
                    std::cout << "Error: Invalid port number (must be 1-65535)\n";
                    std::cout << helpText.toStdString();
                    return -1;
                }
            }
            else
            {
                std::cout << "Error: Port number required\n";
                std::cout << helpText.toStdString();
                return -1;
            }
        }
        else if ((currentArg == "-S") || (currentArg == "--no-web"))
        {
            overrides.noWeb = true;
        }
        else if (currentArg == "--lan")
        {
            if (argc > c + 1) { overrides.lanIP = argv[++c]; }
            else { std::cout << "Error: --lan requires IP address\n"; return -1; }
        }
        else if (currentArg == "--lan-control")
        {
            if (argc > c + 1) { overrides.controlPort = QString(argv[++c]).toInt(); }
            else { std::cout << "Error: --lan-control requires port\n"; return -1; }
        }
        else if (currentArg == "--lan-serial")
        {
            if (argc > c + 1) { overrides.serialPort = QString(argv[++c]).toInt(); }
            else { std::cout << "Error: --lan-serial requires port\n"; return -1; }
        }
        else if (currentArg == "--lan-audio")
        {
            if (argc > c + 1) { overrides.audioPort = QString(argv[++c]).toInt(); }
            else { std::cout << "Error: --lan-audio requires port\n"; return -1; }
        }
        else if (currentArg == "--lan-user")
        {
            if (argc > c + 1) { overrides.lanUser = argv[++c]; }
            else { std::cout << "Error: --lan-user requires username\n"; return -1; }
        }
        else if (currentArg == "--lan-pass")
        {
            if (argc > c + 1) { overrides.lanPass = argv[++c]; }
            else { std::cout << "Error: --lan-pass requires password\n"; return -1; }
        }
        else if (currentArg == "--civ")
        {
            if (argc > c + 1) {
                QString civStr = QString(argv[++c]).trimmed();
                bool ok = false;
                int civ = 0;
                if (civStr.startsWith("0x", Qt::CaseInsensitive))
                    civ = civStr.mid(2).toInt(&ok, 16);
                else
                    civ = civStr.toInt(&ok, 10);
                if (!ok || civ < 0 || civ > 0xFF) {
                    std::cout << "Error: --civ value must be 0x00-0xFF or 0-255 (e.g. 0x94 or 148)\n";
                    return -1;
                }
                overrides.civAddr = civ;
            }
            else { std::cout << "Error: --civ requires address\n"; return -1; }
        }
        else if (currentArg == "--serial-port")
        {
            if (argc > c + 1) { overrides.usbPort = argv[++c]; }
            else { std::cout << "Error: --serial-port requires device path\n"; return -1; }
        }
        else if (currentArg == "--audio-system")
        {
            if (argc > c + 1) { overrides.audioSystem = QString(argv[++c]).toInt(); }
            else { std::cout << "Error: --audio-system requires ID (0=Qt, 1=PortAudio, 2=RtAudio)\n"; return -1; }
        }
        else if (currentArg == "--audio-device")
        {
            if (argc > c + 1) {
                QString devArg = argv[++c];
                if (devArg.compare("list", Qt::CaseInsensitive) == 0) {
                    std::cout << "Available audio input devices:\n";
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    for (const auto &dev : QAudioDeviceInfo::availableDevices(QAudio::AudioInput)) {
                        std::cout << "  " << dev.deviceName().toStdString() << "\n";
                    }
#else
                    for (const QAudioDevice &dev : QMediaDevices::audioInputs()) {
                        std::cout << "  " << dev.description().toStdString() << "\n";
                    }
#endif
                    std::cout << "\nAvailable audio output devices:\n";
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    for (const auto &dev : QAudioDeviceInfo::availableDevices(QAudio::AudioOutput)) {
                        std::cout << "  " << dev.deviceName().toStdString() << "\n";
                    }
#else
                    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
                        std::cout << "  " << dev.description().toStdString() << "\n";
                    }
#endif
                    return 0;
                }
                overrides.audioDevice = devArg;
            }
            else { std::cout << "Error: --audio-device requires device name (e.g. 'USB Audio CODEC')\n"; return -1; }
        }
        else if (currentArg == "--manufacturer")
        {
            if (argc > c + 1) { overrides.manufacturer = QString(argv[++c]).toInt(); }
            else { std::cout << "Error: --manufacturer requires ID\n"; return -1; }
        }
        else if ((currentArg == "-?") || (currentArg == "--help"))
        {
            std::cout << helpText.toStdString();
            return 0;
        }
        else if ((currentArg == "-v") || (currentArg == "--version"))
        {
            std::cout << version.toStdString();
            return 0;
	}
        else {
            std::cout << "Unrecognized option: " << currentArg.toStdString();
            std::cout << helpText.toStdString();
            return -1;
        }

    }

#ifdef BUILD_WFSERVER

    // Set the logging file before doing anything else.
    m_logFile.reset(new QFile(logFilename));
    // Open the file logging
    m_logFile.data()->open(QFile::WriteOnly | QFile::Truncate | QFile::Text);
    // Set handler
    qInstallMessageHandler(messageHandler);

    qInfo(logSystem()) << version;

#endif

#ifdef BUILD_WFSERVER
 #ifdef Q_OS_WIN
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)cleanup, TRUE);
 #else
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGKILL, cleanup);
 #endif
    kb->start();
    w = new servermain(settingsFile, overrides);
#else
    a.setWheelScrollLines(1); // one line per wheel click
    wfmain w(settingsFile, logFilename, debugMode, webPort);
    w.show();

#endif
    return a.exec();

}

#ifdef BUILD_WFSERVER

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Open stream file writes
    if (type == QtDebugMsg && !debugMode)
    {
        return;
    }
#ifdef Q_OS_WIN
    // Qt 5.15 + OpenSSL 3.x: suppress noise about removed 1.1 symbols
    if (msg.contains("cannot call unresolved function"))
        return;
#endif
    QMutexLocker locker(&logMutex);
    QTextStream out(m_logFile.data());
    QString text;

    // Write the date of recording
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
    // By type determine to what level belongs message
    
    switch (type)
    {
        case QtDebugMsg:
            out << "DBG ";
            break;
        case QtInfoMsg:
            out << "INF ";
            break;
        case QtWarningMsg:
            out << "WRN ";
            break;
        case QtCriticalMsg:
            out << "CRT ";
            break;
        case QtFatalMsg:
            out << "FTL ";
            break;
    } 
    // Write to the output category of the message and the message itself
    out << context.category << ": " << msg << "\n";
    std::cout << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ").toLocal8Bit().toStdString() << msg.toLocal8Bit().toStdString() << "\n";
    out.flush();    // Clear the buffered data
}
#endif
