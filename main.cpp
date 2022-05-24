#ifdef BUILD_WFSERVER
#include <QtCore/QCoreApplication>
#include "keyboard.h"
#else
#include <QApplication>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <csignal>
#endif

#include <iostream>
#include "wfmain.h"
#include "logcategories.h"

// Copyright 2017-2021 Elliott H. Liggett

// Smart pointer to log file
QScopedPointer<QFile>   m_logFile;
QMutex logMutex;
bool debugMode=false;

#ifdef BUILD_WFSERVER
    servermain* w=Q_NULLPTR;

    #ifdef Q_OS_WIN
    bool __stdcall cleanup(DWORD sig)
    #else
    static void cleanup(int sig)
    #endif
    {
        Q_UNUSED(sig)
        qDebug() << "Exiting via SIGNAL";
        if (w!=Q_NULLPTR) w->deleteLater();
        QCoreApplication::quit();

        #ifdef Q_OS_WIN
            return true;
        #else
            return;
        #endif
    }

#endif

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

int main(int argc, char *argv[])
{

#ifdef BUILD_WFSERVER
    QCoreApplication a(argc, argv);
    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");
    a.setApplicationName("wfserver");
    keyboard* kb = new keyboard();
    kb->start();
#else
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);
    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");
    a.setApplicationName("wfview");
#endif

#ifdef QT_DEBUG
    debugMode = true;
#endif

    QString serialPortCL;
    QString hostCL;
    QString civCL;
#ifdef Q_OS_MAC
    QString logFilename= QStandardPaths::standardLocations(QStandardPaths::DownloadLocation)[0] + "/wfview.log";
#else
    QString logFilename= QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0] + "/wfview.log";
#endif
    QString settingsFile = NULL;
    QString currentArg;


    const QString helpText = QString("\nUsage: -p --port /dev/port, -h --host remotehostname, -c --civ 0xAddr, -l --logfile filename.log, -s --settings filename.ini, -d --debug, -v --version\n"); // TODO...
#ifdef BUILD_WFSERVER
    const QString version = QString("wfserver version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
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
#endif
    for(int c=1; c<argc; c++)
    {
        //qInfo() << "Argc: " << c << " argument: " << argv[c];
        currentArg = QString(argv[c]);

        if ((currentArg == "-p") || (currentArg == "--port"))
        {
            if (argc > c)
            {
                serialPortCL = argv[c + 1];
                c += 1;
            }
        }
        else if ((currentArg == "-d") || (currentArg == "--debug"))
        {
            debugMode = true;
        } 
        else if ((currentArg == "-h") || (currentArg == "--host"))
        {
            if(argc > c)
            {
                hostCL = argv[c+1];
                c+=1;
            }
        }
        else if ((currentArg == "-c") || (currentArg == "--civ"))
        {
            if (argc > c)
            {
                civCL = argv[c + 1];
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
        } else {
            std::cout << "Unrecognized option: " << currentArg.toStdString();
            std::cout << helpText.toStdString();
            return -1;
        }

    }

    // Set the logging file before doing anything else.
    m_logFile.reset(new QFile(logFilename));
    // Open the file logging
    m_logFile.data()->open(QFile::WriteOnly | QFile::Truncate | QFile::Text);
    // Set handler
    qInstallMessageHandler(messageHandler);

    qInfo(logSystem()) << version;
    qDebug(logSystem()) << QString("SerialPortCL as set by parser: %1").arg(serialPortCL);
    qDebug(logSystem()) << QString("remote host as set by parser: %1").arg(hostCL);
    qDebug(logSystem()) << QString("CIV as set by parser: %1").arg(civCL);

#ifdef BUILD_WFSERVER
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)cleanup, TRUE);
#else
    signal(SIGINT, cleanup);
    signal(SIG_TERM, cleanup);
    signal(SIG_CLOSE, cleanup);
#endif
    w = new servermain(serialPortCL, hostCL, settingsFile);
#else
    a.setWheelScrollLines(1); // one line per wheel click
    wfmain w(serialPortCL, hostCL, settingsFile);
    w.show();
    
#endif
    return a.exec();

}


void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Open stream file writes
    if (type == QtDebugMsg && !debugMode)
    {
        return;
    }
    QMutexLocker locker(&logMutex);
    QTextStream out(m_logFile.data());

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
#ifdef BUILD_WFSERVER
    std::cout << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ").toLocal8Bit().toStdString() << msg.toLocal8Bit().toStdString() << "\n";
#endif
    out.flush();    // Clear the buffered data
}
