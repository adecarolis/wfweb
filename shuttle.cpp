#pragma comment (lib, "Setupapi.lib")
#include "shuttle.h"
#include <QDebug>

shuttle::shuttle()
{
	qInfo() << "Starting HID USB device detection";
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
                // No devices found, schedule another check in 1000ms
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
        qInfo() << QString("Found Device: %0 from %1").arg(QString::fromWCharArray(product)).arg(QString::fromWCharArray(manufacturer));
        hid_set_nonblocking(handle, 1);
        QTimer::singleShot(0, this, SLOT(runTimer()));
    }
}

void shuttle::runTimer()
{
    int res=1;
    while (res > 0) {
        QByteArray data(HIDDATALENGTH, 0x0);
        res = hid_read(handle, (unsigned char*)data.data(), HIDDATALENGTH);
        if (res < 0)
        {
            qInfo() << "USB Device disconnected?";
            hid_close(handle);
            QTimer::singleShot(1000, this, SLOT(run()));
            return;
        }
        else if (res == 5 && (usbDevice == shuttleXpress || usbDevice == shuttlePro2))
        {
            data.resize(res);
            qDebug() << "Shuttle Data received: " << hex << (unsigned char)data[0] << ":"
                << hex << (unsigned char)data[1] << ":"
                << hex << (unsigned char)data[2] << ":"
                << hex << (unsigned char)data[3] << ":"
                << hex << (unsigned char)data[4];

            unsigned int tempButtons = (unsigned int)((unsigned char)data[3] | (unsigned char)data[4] << 8);
            unsigned char tempJogpos = (unsigned char)data[1];
            unsigned char tempShutpos = (unsigned char)data[0];

            if (tempJogpos == jogpos + 1 || (tempJogpos == 0 && jogpos == 0xff))
            {
                qDebug() << "JOG PLUS";
                emit jogPlus();
            }
            else if (tempJogpos != jogpos) {
                qDebug() << "JOG MINUS";
                emit jogMinus();
            }

            /* Button matrix:
                1000000000000000 = button15
                0100000000000000 = button14
                0010000000000000 = button13
                0001000000000000 = button12
                0000100000000000 = button11
                0000010000000000 = button10
                0000001000000000 = button9
                0000000100000000 = button8 - xpress0
                0000000010000000 = button7 - xpress1
                0000000001000000 = button6 - xpress2
                0000000000100000 = button5 - xpress3
                0000000000010000 = button4 - xpress4
                0000000000001000 = button3
                0000000000000100 = button2
                0000000000000010 = button1
                0000000000000001 = button0
            */
            if (buttons != tempButtons)
            {
                qDebug() << "BUTTON: " << qSetFieldWidth(16) << bin << tempButtons;

                // Step through all buttons and emit ones that have been pressed.
                for (unsigned char i = 0; i < 16; i++)
                {
                    if ((tempButtons >> i & 1) && !(buttons >> i & 1))
                        emit button(true, i);
                    else if ((buttons >> i & 1) && !(tempButtons >> i & 1))
                        emit button(false, i);
                }
            }

            buttons = tempButtons;
            jogpos = tempJogpos;
            shutpos = tempShutpos;

        }
        else if (res == 64 && usbDevice == RC28)
        {
            // This is a response from the Icom RC28
            data.resize(8); // Might as well get rid of the unused data.
            if (((unsigned char)data[5] == 0x06) && ((unsigned char)lastData[5] != 0x06))
            {
                emit button(true, 6);
            }
            else if (((unsigned char)data[5] != 0x06) && ((unsigned char)lastData[5] == 0x06))
            {
                emit button(false, 6);
            }
            else if (((unsigned char)data[5] == 0x03) && ((unsigned char)lastData[5] != 0x03))
            {
                emit button(true, 7);
            }
            else if (((unsigned char)data[5] != 0x03) && ((unsigned char)lastData[5] == 0x03))
            {
                emit button(false, 7);
            }
            else if (((unsigned char)data[5] == 0x7d) && ((unsigned char)lastData[5] != 0x7d))
            {
                emit button(true, 5);
            }
            else if (((unsigned char)data[5] != 0x7d) && ((unsigned char)lastData[5] == 0x7d))
            {
                emit button(false, 5);
            }

            if ((unsigned char)data[5] == 0x07)
            {
                if ((unsigned char)data[3]==0x01)
                {
                    qDebug() << "Frequency UP";
                        emit jogPlus();
                }
                else if ((unsigned char)data[3] == 0x02)
                {
                    qDebug() << "Frequency DOWN";
                        emit jogMinus();
                }
            }
            lastData = data;
        }

        if (lastShuttle.msecsTo(QTime::currentTime()) >= 1000)
        {
            if (shutpos > 0 && shutpos < 8)
            {
                shutMult = shutpos;
                emit doShuttle(true, shutMult);
                qInfo() << "SHUTTLE PLUS" << shutMult;

            }
            else if (shutpos <= 0xff && shutpos >= 0xf0) {
                shutMult = abs(shutpos - 0xff) + 1;
                emit doShuttle(false, shutMult);
                qInfo() << "SHUTTLE MINUS" << shutMult;
            }
            lastShuttle = QTime::currentTime();
        }

    }
    // Run every 25ms
    QTimer::singleShot(25, this, SLOT(runTimer()));
}

#define BIT_CLEAR(a,b) ((a) &= ~(1ULL<<(b)))
void shuttle::ledControl(bool on, unsigned char num)
{
    QByteArray data(9,0x0);
    data[0] = 8;
    data[1] = 0x01;
    unsigned char ledNum=0x07;
    if (on)
        ledNum &= ~(1ULL << (num - 1));

    data[2] = ledNum;

    int res = hid_write(handle, (const unsigned char*)data.constData(), 8);

    if (res < 0) {
        qDebug() << "Unable to write(), Error:" << hid_error(handle);
        return;
    }

    qDebug() << "write() success";

}