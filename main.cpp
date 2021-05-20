#include <QApplication>
#include <iostream>
#include "wfmain.h"
#include "logcategories.h"

// Copytight 2017-2021 Elliott H. Liggett

// Smart pointer to log file
QScopedPointer<QFile>   m_logFile;
QMutex logMutex;
bool debugMode=false;

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    //a.setStyle( "Fusion" );

    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");
    a.setApplicationName("wfview");

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


    const QString helpText = QString("\nUsage: -p --port /dev/port, -h --host remotehostname, -c --civ 0xAddr, -l --logfile filename.log, -s --settings filename.ini, -d --debug\n"); // TODO...

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
        else if ((currentArg == "--help"))
        {
            std::cout << helpText.toStdString();
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
    m_logFile.data()->open(QFile::Append | QFile::Text);
    // Set handler
    qInstallMessageHandler(messageHandler);

    qInfo(logSystem()) << "Starting wfview";


    qDebug(logSystem()) << "SerialPortCL as set by parser: " << serialPortCL;
    qDebug(logSystem()) << "remote host as set by parser: " << hostCL;
    qDebug(logSystem()) << "CIV as set by parser: " << civCL;
    a.setWheelScrollLines(1); // one line per wheel click
    wfmain w( serialPortCL, hostCL, settingsFile);

    w.show();



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
    out.flush();    // Clear the buffered data
}
