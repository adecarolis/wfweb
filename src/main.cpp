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

#include <iostream>
#include "wfmain.h"

#include "logcategories.h"

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
    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");
    a.setApplicationName("wfweb");
    kb = new keyboard();
    kb->start();
#else
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QApplication a(argc, argv);
    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");
    a.setApplicationName("wfview");
#endif

#ifdef QT_DEBUG
    //debugMode = true;
#endif

    QDateTime date = QDateTime::currentDateTime();
    QString formattedTime = date.toString("dd.MM.yyyy hh:mm:ss");
    QString logFilename = (QString("%1/%2-%3.log").arg(QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0]).arg(a.applicationName()).arg(date.toString("yyyyMMddhhmmss")));

    QString settingsFile = NULL;
    QString currentArg;
    quint16 webPort = 0;  // 0 means use settings file value


    const QString helpText = QString("\nUsage: -l --logfile filename.log, -s --settings filename.ini, -c --clearconfig CONFIRM, -b --background (not Windows), -d --debug, -p --port <port>, -v --version\n"); // TODO...
#ifdef BUILD_WFSERVER
    const QString version = QString("wfweb version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFVIEW_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());
#else
    const QString version = QString("wfview version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFVIEW_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());

    // Translator doesn't really make sense for wfweb right now.
    QTranslator myappTranslator;
    qDebug() << "Current translation language: " << myappTranslator.language();

    bool trResult = myappTranslator.load(QLocale(), QLatin1String("wfview"), QLatin1String("_"), QLatin1String(":/translations"));
    if(trResult) {
        qDebug() << "Recognized requested language and loaded the translations (or at least found the /translations resource folder). Installing translator.";
        a.installTranslator(&myappTranslator);
    } else {
        qDebug() << "Could not load translation.";
    }

    qDebug() << "Changed to translation language: " << myappTranslator.language();
#endif

    for(int c=1; c<argc; c++)
    {
        //qInfo() << "Argc: " << c << " argument: " << argv[c];
        currentArg = QString(argv[c]);

        if ((currentArg == "-d") || (currentArg == "--debug"))
        {
            debugMode = true;
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
                    std::cout << QString("All wfview settings cleared.\n").toStdString();
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
                    webPort = static_cast<quint16>(port);
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
    w = new servermain(settingsFile, webPort);
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
