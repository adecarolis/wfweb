#include "wfmain.h"
#include <QApplication>
#include <iostream>

// Copytight 2017-2021 Elliott H. Liggett


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    //a.setStyle( "Fusion" );

    a.setOrganizationName("eliggett");
    a.setOrganizationDomain("nodomain");
    a.setApplicationName("wfview");



    QString serialPortCL;
    QString hostCL;
    QString civCL;

    QString currentArg;

    const QString helpText = QString("Usage: -p --port /dev/port, -h --host remotehostname, -c --civ 0xAddr"); // TODO...

    for(int c=1; c<argc; c++)
    {
        //qDebug() << "Argc: " << c << " argument: " << argv[c];
        currentArg = QString(argv[c]);

        if((currentArg == "-p") || currentArg == "--port")
        {
            if(argc > c)
            {
                serialPortCL = argv[c+1];
                c+=1;
            }
        } else if ((currentArg == "-h") || (currentArg == "--host"))
        {
            if(argc > c)
            {
                hostCL = argv[c+1];
                c+=1;
            }
        } else if ((currentArg == "-c") || (currentArg == "--civ"))
        {
            if(argc > c)
            {
                civCL = argv[c+1];
                c+=1;
            }
        } else if ((currentArg == "--help"))
        {
            std::cout << helpText.toStdString();
            return 0;
        } else {
            std::cout << "Unrecognized option: " << currentArg.toStdString();
            std::cout << helpText.toStdString();
            return -1;
        }

    }


#ifdef QT_DEBUG
    qDebug() << "SerialPortCL as set by parser: " << serialPortCL;
    qDebug() << "remote host as set by parser: " << hostCL;
    qDebug() << "CIV as set by parser: " << civCL;
#endif
    a.setWheelScrollLines(1); // one line per wheel click
    wfmain w( serialPortCL, hostCL);

    w.show();



    return a.exec();
}
