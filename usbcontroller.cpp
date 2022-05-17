#pragma comment (lib, "Setupapi.lib")
#include "usbcontroller.h"
#include <QDebug>

#include "logcategories.h"

usbController::usbController()
{
	qInfo(logUsbControl()) << "Starting usbController()";
}

usbController::~usbController()
{
    qInfo(logUsbControl) << "Ending usbController()";
    hid_close(handle);
    hid_exit();
    for (BUTTON& b : buttonList)
    {
        if (b.onText)
            delete b.onText;
        if (b.offText)
            delete b.offText;
    }
}

void usbController::init()
{
}


int usbController::hidApiWrite(unsigned char* data, unsigned char length)
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


void usbController::run()
{
/*
    commands.append(COMMAND(0, "None", cmdNone, 0x0));
    commands.append(COMMAND(1, "PTT On", cmdSetPTT, 0x1));
    commands.append(COMMAND(2, "PTT Off", cmdSetPTT, 0x0));
    commands.append(COMMAND(3, "PTT Toggle", cmdSetPTT, 0x1));
    commands.append(COMMAND(4, "Tune", cmdNone, 0x0));
    commands.append(COMMAND(5, "Step+", cmdNone, 0x0));
    commands.append(COMMAND(6, "Step-", cmdNone, 0x0));
    commands.append(COMMAND(7, "Mode+", cmdNone, 0x0));
    commands.append(COMMAND(8, "Mode-", cmdNone, 0x0));
    commands.append(COMMAND(9, "Band+", cmdNone, 0x0));
    commands.append(COMMAND(10, "Band-", cmdNone, 0x0));
    commands.append(COMMAND(9, "NR", cmdNone, 0x0));
    commands.append(COMMAND(10, "NB", cmdNone, 0x0));
    commands.append(COMMAND(11, "AGC", cmdNone, 0x0));
    commands.append(COMMAND(12, "NB", cmdNone, 0x0));
    commands.append(COMMAND(14, "23cm", cmdGetBandStackReg, band23cm));
    commands.append(COMMAND(15, "70cm", cmdGetBandStackReg, band70cm));
    commands.append(COMMAND(16, "2m", cmdGetBandStackReg, band2m));
    commands.append(COMMAND(17, "AIR", cmdGetBandStackReg, bandAir));
    commands.append(COMMAND(18, "WFM", cmdGetBandStackReg, bandWFM));
    commands.append(COMMAND(19, "4m", cmdGetBandStackReg, band4m));
    commands.append(COMMAND(20, "6m", cmdGetBandStackReg, band6m));
    commands.append(COMMAND(21, "10m", cmdGetBandStackReg, band10m));
    commands.append(COMMAND(22, "12m", cmdGetBandStackReg, band12m));
    commands.append(COMMAND(23, "15m", cmdGetBandStackReg, band15m));
    commands.append(COMMAND(24, "17m", cmdGetBandStackReg, band17m));
    commands.append(COMMAND(25, "20m", cmdGetBandStackReg, band20m));
    commands.append(COMMAND(26, "30m", cmdGetBandStackReg, band30m));
    commands.append(COMMAND(27, "40m", cmdGetBandStackReg, band40m));
    commands.append(COMMAND(28, "60m", cmdGetBandStackReg, band60m));
    commands.append(COMMAND(29, "80m", cmdGetBandStackReg, band80m));
    commands.append(COMMAND(30, "160m", cmdGetBandStackReg, band160m));
    commands.append(COMMAND(31, "630m", cmdGetBandStackReg, band630m));
    commands.append(COMMAND(32, "2200m", cmdGetBandStackReg, band2200m));
    commands.append(COMMAND(33, "GEN", cmdGetBandStackReg, bandGen));

*/

    handle = hid_open(0x0b33, 0x0020, NULL);
    if (!handle) {
        handle = hid_open(0x0b33, 0x0030, NULL);
        if (!handle) {
            handle = hid_open(0x0C26, 0x001E, NULL);
            if (!handle) {
                usbDevice = NONE;
                // No devices found, schedule another check in 1000ms
                QTimer::singleShot(1000, this, SLOT(run()));
            }
            else {
                usbDevice = RC28;
            }
        }
        else {
            usbDevice = shuttlePro2;
            buttonList.clear();
            buttonList.append(BUTTON(0, QRect(60, 66, 40, 30), Qt::red));
            buttonList.append(BUTTON(1, QRect(114, 50, 40, 30), Qt::red));
            buttonList.append(BUTTON(2, QRect(169, 47, 40, 30), Qt::red));
            buttonList.append(BUTTON(3, QRect(225, 59, 40, 30), Qt::red));
            buttonList.append(BUTTON(4, QRect(41, 132, 40, 30), Qt::red));
            buttonList.append(BUTTON(5, QRect(91, 105, 40, 30), Qt::red));
            buttonList.append(BUTTON(6, QRect(144, 93, 40, 30), Qt::red));
            buttonList.append(BUTTON(7, QRect(204, 99, 40, 30), Qt::red));
            buttonList.append(BUTTON(8, QRect(253, 124, 40, 30), Qt::red));
            buttonList.append(BUTTON(9, QRect(50, 270, 70, 55), Qt::red));
            buttonList.append(BUTTON(10, QRect(210, 270, 70, 55), Qt::red));
            buttonList.append(BUTTON(11, QRect(50, 335, 70, 55), Qt::red));
            buttonList.append(BUTTON(12, QRect(210, 335, 70, 55), Qt::red));
            buttonList.append(BUTTON(13, QRect(30, 195, 25, 80), Qt::red));
            buttonList.append(BUTTON(14, QRect(280, 195, 25, 80), Qt::red));

        }
    }
    else {
        usbDevice = shuttleXpress;
        buttonList.append(BUTTON(0, QRect(60, 66, 40, 30), Qt::red));
        buttonList.append(BUTTON(1, QRect(114, 50, 40, 30), Qt::red));
        buttonList.append(BUTTON(2, QRect(169, 47, 40, 30), Qt::red));
        buttonList.append(BUTTON(3, QRect(225, 59, 40, 30), Qt::red));
        buttonList.append(BUTTON(4, QRect(41, 132, 40, 30), Qt::red));

    }

    if (handle)
    {
        int res;
        wchar_t manufacturer[MAX_STR];
        wchar_t product[MAX_STR];
        wchar_t serial[MAX_STR];

        res = hid_get_manufacturer_string(handle, manufacturer, MAX_STR);
        if (res > -1)
        {
            this->manufacturer = QString::fromWCharArray(manufacturer);
        }
 
        res = hid_get_product_string(handle, product, MAX_STR);
        if (res > -1)
        {
            this->product = QString::fromWCharArray(product);
        }

        res = hid_get_serial_number_string(handle, serial, MAX_STR);
        if (res > -1)
        {
            this->serial = QString::fromWCharArray(serial);
        }

        qInfo(logUsbControl()) << QString("Found Device: %0 from %1 S/N %2").arg(this->product).arg(this->manufacturer).arg(this->serial);
        hid_set_nonblocking(handle, 1);
        emit newDevice(usbDevice,&buttonList, &commands); // Let the UI know we have a new controller
        QTimer::singleShot(0, this, SLOT(runTimer()));
    }
}

void usbController::runTimer()
{
    int res=1;
    while (res > 0) {
        QByteArray data(HIDDATALENGTH, 0x0);
        res = hid_read(handle, (unsigned char*)data.data(), HIDDATALENGTH);
        if (res < 0)
        {
            qInfo(logUsbControl()) << "USB Device disconnected" << this->product;
            emit newDevice(0,&buttonList,&commands);
            this->product = "";
            this->manufacturer = "";
            this->serial = "<none>";
            hid_close(handle);
            QTimer::singleShot(1000, this, SLOT(run()));
            return;
        }
        else if (res == 5 && (usbDevice == shuttleXpress || usbDevice == shuttlePro2))
        {
            data.resize(res);
            
            /*qDebug(logUsbControl()) << "usbController Data received " << hex << (unsigned char)data[0] << ":"
                << hex << (unsigned char)data[1] << ":"
                << hex << (unsigned char)data[2] << ":"
                << hex << (unsigned char)data[3] << ":"
                << hex << (unsigned char)data[4];
                */
            unsigned int tempButtons = (unsigned int)((unsigned char)data[3] | (unsigned char)data[4] << 8);
            unsigned char tempJogpos = (unsigned char)data[1];
            unsigned char tempShutpos = (unsigned char)data[0];

            if (tempJogpos == jogpos + 1 || (tempJogpos == 0 && jogpos == 0xff))
            {
                jogCounter++;
                //qDebug(logUsbControl()) << "JOG PLUS" << jogCounter;
            }
            else if (tempJogpos != jogpos) {
                jogCounter--;
                //qDebug(logUsbControl()) << "JOG MINUS" << jogCounter;
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
                //qDebug(logUsbControl()) << "BUTTON: " << qSetFieldWidth(16) << bin << tempButtons;

                // Step through all buttons and emit ones that have been pressed.
                for (unsigned char i = 0; i < 16; i++)
                {
                    if ((tempButtons >> i & 1) && !(buttons >> i & 1)) 
{
                        if (i < buttonList.size() && buttonList[i].onCommand != Q_NULLPTR && buttonList[i].onCommand->index > 0) {
                            qDebug() << "On Button event:" << buttonList[i].onCommand->text;
                            emit button(buttonList[i].onCommand);
                        }
                    }
                    else if ((buttons >> i & 1) && !(tempButtons >> i & 1))
                    {
                        if (i < buttonList.size() && buttonList[i].offCommand != Q_NULLPTR && buttonList[i].offCommand->index > 0) {
                            qDebug() << "Off Button event:" << buttonList[i].offCommand->text;
                            emit button(buttonList[i].offCommand);
                        }
                    }
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

            if (lastData.size() != 8) {
                lastData = data;
            }

            if (((unsigned char)data[5] == 0x06) && ((unsigned char)lastData[5] != 0x06))
            {
                //emit button(true, 6);
            }
            else if (((unsigned char)data[5] != 0x06) && ((unsigned char)lastData[5] == 0x06))
            {
                //emit button(false, 6);
            }
            else if (((unsigned char)data[5] == 0x03) && ((unsigned char)lastData[5] != 0x03))
            {
                //emit button(true, 7);
            }
            else if (((unsigned char)data[5] != 0x03) && ((unsigned char)lastData[5] == 0x03))
            {
                //emit button(false, 7);
            }
            else if (((unsigned char)data[5] == 0x7d) && ((unsigned char)lastData[5] != 0x7d))
            {
                //emit button(true, 5);
            }
            else if (((unsigned char)data[5] != 0x7d) && ((unsigned char)lastData[5] == 0x7d))
            {
                //emit button(false, 5);
            }

            if ((unsigned char)data[5] == 0x07)
            {
                if ((unsigned char)data[3] == 0x01)
                {
                    //qDebug(logUsbControl()) << "Frequency UP";
                    jogCounter++;
                    //emit jogPlus();
                }
                else if ((unsigned char)data[3] == 0x02)
                {
                    //qDebug(logUsbControl()) << "Frequency DOWN";
                    emit jogMinus();
                    jogCounter--;
                }
            }

            lastData = data;
        }

        if (lastusbController.msecsTo(QTime::currentTime()) >= 500 || lastusbController > QTime::currentTime())
        {
            if (shutpos < 0x08)
            {
                shutMult = shutpos;
                emit doShuttle(true, shutMult);
                //qDebug(logUsbControl()) << "Shuttle PLUS" << shutMult;

            }
            else if (shutpos > 0xEF) {
                shutMult = abs(shutpos - 0xff) + 1;
                emit doShuttle(false, shutMult);
                //qDebug(logUsbControl()) << "Shuttle MINUS" << shutMult;
            }
            if (jogCounter != 0) {
                emit sendJog(jogCounter); 
                //qDebug(logUsbControl()) << "Change Frequency by" << jogCounter << "hz";
                jogCounter = 0;
            }

            lastusbController = QTime::currentTime();
        }

    }
    // Run every 25ms
    QTimer::singleShot(25, this, SLOT(runTimer()));
}

void usbController::ledControl(bool on, unsigned char num)
{
    if (usbDevice == RC28) {
        QByteArray data(9, 0x0);
        data[0] = 8;
        data[1] = 0x01;
        unsigned char ledNum = 0x07;
        if (on)
            ledNum &= ~(1ULL << (num - 1));

        data[2] = ledNum;

        int res = hid_write(handle, (const unsigned char*)data.constData(), 8);

        if (res < 0) {
            qDebug(logUsbControl()) << "Unable to write(), Error:" << hid_error(handle);
            return;
        }

        qDebug(logUsbControl()) << "write() success";
    }
}