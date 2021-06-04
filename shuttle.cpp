#include "shuttle.h"
#include <QDebug>

shuttle::shuttle()
{

}

shuttle::~shuttle()
{
    qDebug() << "************ Ending HID";
    hid_close(handle);
    hid_exit();
}

void shuttle::init()
{

}


int shuttle::hidApiWrite(unsigned char* data, unsigned char length)
{
/*    int res;
    unsigned char realData[length + 1];

    realData[0] = length;
    int i;
    for (i = 0; i < length; i++)
    {
        realData[i + 1] = data[i];
    }

    res = hid_write(handle, realData, length + 1);
    if (res < 0) {
        printf("Unable to write()\n");
        printf("Error: %ls\n", hid_error(handle));
        return -1;
    }

    printf("write success\n");
    */
    return 0;
}


void shuttle::run()
{
    handle = hid_open(0x0b33, 0x0020, NULL);
    if (!handle) {
        qDebug() << "unable to open device";
        return;
    }

    qDebug() << "*************** open device success";

    hid_set_nonblocking(handle, 1);
    qDebug() << "************  Starting HID";
    QTimer::singleShot(0, this, SLOT(runTimer()));
}

void shuttle::runTimer()
{
    int res;
    QByteArray data(HIDDATALENGTH,0x0);
    res = hid_read(handle, (unsigned char *)data.data(), HIDDATALENGTH);
    if (res == 0)
        ;//printf("waiting...\n");
    else if (res < 0)
    {
        qDebug() << "Unable to read()";
        return;
    }
    else if (res == 5)
    {
        data.resize(res);
        qDebug() << "Shuttle Data received: " << hex << (unsigned char)data[0] << ":"
            << hex << (unsigned char)data[1] << ":"
            << hex << (unsigned char)data[2] << ":"
            << hex << (unsigned char)data[3] << ":"
            << hex << (unsigned char)data[4];

        unsigned char tempButtons = ((unsigned char)data[3] | (unsigned char)data[4]);
        unsigned char tempJogpos = (unsigned char)data[1];
        unsigned char tempShutpos = (unsigned char)data[0];


        if (tempJogpos == jogpos + 1 || tempJogpos == jogpos - 1 || tempJogpos == 0xff || tempJogpos == 0x00) {
            if (tempJogpos > jogpos || (tempJogpos == 0x00 && jogpos == 0xff))
            {
                qDebug() << "JOG UP";
            }
            else {
                qDebug() << "JOG DOWN";
            }
        }
        if (tempShutpos == shutpos + 1) 
        {
            qDebug() << "SHUTTLE UP";
        }
        else if (tempShutpos == shutpos-1) {
            qDebug() << "SHUTTLE DOWN";
        }
        buttons = ((unsigned char)data[3] | (unsigned char)data[4]);
        jogpos = (unsigned char)data[1];
        shutpos = (unsigned char)data[0];

        emit hidDataArrived(data);
    }
    // Run every 50ms
    QTimer::singleShot(50, this, SLOT(runTimer()));
}

