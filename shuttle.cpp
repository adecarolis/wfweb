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
    Q_UNUSED(data);
    Q_UNUSED(length);
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
        handle = hid_open(0x0b33, 0x0030, NULL);
        if (!handle) {
            handle = hid_open(0x0C26, 0x001E, NULL);
            if (!handle) {
                QTimer::singleShot(1000, this, SLOT(run()));
            }
            else {
                usbDevice = RC28;
            }
        }
        else {
            usbDevice = shuttlePro2;
        }
    }
    else {
        usbDevice = shuttleXpress;
    }
    
    if (handle)
    {
        int res;
        wchar_t manufacturer[MAX_STR];
        wchar_t product[MAX_STR];
        res = hid_get_manufacturer_string(handle, manufacturer, MAX_STR);
        res = hid_get_product_string(handle, product, MAX_STR);
        qDebug() << QString("Found Device: %0 from %1").arg(QString::fromWCharArray(product)).arg(QString::fromWCharArray(manufacturer));
        hid_set_nonblocking(handle, 1);
        QTimer::singleShot(0, this, SLOT(runTimer()));
    }
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
        qDebug() << "USB Device disconnected?";
        hid_close(handle);
        QTimer::singleShot(1000, this, SLOT(run()));
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

        unsigned int tempButtons = unsigned int((unsigned char)data[3] | (unsigned char)data[4] << 8);
        unsigned char tempJogpos = (unsigned char)data[1];
        unsigned char tempShutpos = (unsigned char)data[0];

        if (tempJogpos == jogpos +1 || (tempJogpos == 0 && jogpos == 0xff))
        {
            qDebug() << "JOG PLUS";
            emit jogPlus();
        }
        else if (tempJogpos != jogpos) {
            qDebug() << "JOG MINUS";
            emit jogMinus();
        }


        if (tempShutpos == shutpos + 1) 
        {
            qDebug() << "SHUTTLE PLUS";
        }
        else if (tempShutpos == shutpos-1) {
            qDebug() << "SHUTTLE MINUS";
        }
        /* Button matrix:
            1000000000000000 = button15
            0100000000000000 = button14
            0010000000000000 = button13
            0001000000000000 = button12
            0000100000000000 = button11
            0000010000000000 = button10
            0000001000000000 = button9
            0000000100000000 = button8 - xpress1
            0000000010000000 = button7 - xpress2
            0000000001000000 = button6 - xpress3
            0000000000100000 = button5 - xpress4
            0000000000010000 = button4 - xpress5
            0000000000001000 = button3
            0000000000000100 = button2
            0000000000000010 = button1
            0000000000000001 = button0
        */
        if (buttons != tempButtons)
        {
            qDebug() << "BUTTON: " << qSetFieldWidth(16) << bin << tempButtons;

            // Step size down
            if ((tempButtons >> 5 & 1) && !(buttons >> 5 & 1))
                emit button5(true);
            // PTT on and off
            if ((tempButtons >> 6 & 1) && !(buttons >> 6 & 1))
                emit button6(true);
            else if ((buttons >> 6 & 1) && !(tempButtons >> 6 & 1))
                emit button6(false);
            // Step size up
            if ((tempButtons >> 7 & 1) && !(buttons >> 7 & 1))
                emit button7(true);
        }

        buttons = unsigned int((unsigned char)data[3] | (unsigned char)data[4] << 8);
        jogpos = (unsigned char)data[1];
        shutpos = (unsigned char)data[0];

    }
    // Run every 25ms
    QTimer::singleShot(25, this, SLOT(runTimer()));
}

