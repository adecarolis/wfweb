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
        qDebug() << "Shuttle Data received: " << hex << (quint8)data[0] << ":"
            << hex << (quint8)data[1] << ":"
            << hex << (quint8)data[2] << ":"
            << hex << (quint8)data[3] << ":"
            << hex << (quint8)data[4];

        emit hidDataArrived(data);
    }
    // Run every 50ms
    QTimer::singleShot(50, this, SLOT(runTimer()));
}

