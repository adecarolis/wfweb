
#include "usbcontroller.h"

#ifdef Q_OS_WIN
#pragma comment (lib, "Setupapi.lib")
#endif

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
    if (gamepad != Q_NULLPTR)
    {
        delete gamepad;
        gamepad = Q_NULLPTR;
    }
}

void usbController::init()
{
}

void usbController::receiveCommands(QVector<COMMAND>* cmds)
{
    qDebug(logUsbControl()) << "Receiving commands";
    commands = cmds;
}

void usbController::receiveButtons(QVector<BUTTON>* buts)
{
    qDebug(logUsbControl()) << "Receiving buttons";
    buttonList = buts;
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

    if (commands == Q_NULLPTR) {
        // We are not ready yet, commands haven't been loaded!
        QTimer::singleShot(1000, this, SLOT(run()));
        return;
    }

    if (gamepad == Q_NULLPTR) {
        auto gamepads = QGamepadManager::instance()->connectedGamepads();
        if (!gamepads.isEmpty()) {
            qInfo(logUsbControl()) << "Found" << gamepads.size() << "Gamepad controllers";
            // If we got here, we have detected a gamepad of some description!
            gamepad = new QGamepad(*gamepads.begin(), this);
            qInfo(logUsbControl()) << "Gamepad 0 is " << gamepad->name();

            if (gamepad->name() == "Microsoft X-Box 360 pad 0")
            {
                usbDevice = xBoxGamepad;
            }
            else {
                usbDevice = unknownGamepad;
            }
            connect(gamepad, &QGamepad::buttonDownChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Button Down" << value;
            });
            connect(gamepad, &QGamepad::buttonUpChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Button Up" << value;
            });
            connect(gamepad, &QGamepad::buttonLeftChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Button Right" << value;
            });
            connect(gamepad, &QGamepad::buttonRightChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Button Down" << value;
            });
            connect(gamepad, &QGamepad::buttonCenterChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Button Center" << value;
            });
            connect(gamepad, &QGamepad::axisLeftXChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Left X" << value;
            });
            connect(gamepad, &QGamepad::axisLeftYChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Left Y" << value;
            });
            connect(gamepad, &QGamepad::axisRightXChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Right X" << value;
            });
            connect(gamepad, &QGamepad::axisRightYChanged, this, [](double value) {
                qInfo(logUsbControl()) << "Right Y" << value;
            });
            connect(gamepad, &QGamepad::buttonAChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button A" << pressed;
                this->buttonState("A", pressed);
            });
            connect(gamepad, &QGamepad::buttonBChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button B" << pressed;
                this->buttonState("B", pressed);
            });
            connect(gamepad, &QGamepad::buttonXChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button X" << pressed;
                this->buttonState("X", pressed);
            });
            connect(gamepad, &QGamepad::buttonYChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Y" << pressed;
                this->buttonState("Y", pressed);
            });
            connect(gamepad, &QGamepad::buttonL1Changed, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button L1" << pressed;
                this->buttonState("L1", pressed);
            });
            connect(gamepad, &QGamepad::buttonR1Changed, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button R1" << pressed;
                this->buttonState("R1", pressed);
            });
            connect(gamepad, &QGamepad::buttonL2Changed, this, [](double value) {
                qInfo(logUsbControl()) << "Button L2: " << value;
            });
            connect(gamepad, &QGamepad::buttonR2Changed, this, [](double value) {
                qInfo(logUsbControl()) << "Button R2: " << value;
            });
            connect(gamepad, &QGamepad::buttonSelectChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Select" << pressed;
                this->buttonState("SELECT", pressed);
            });
            connect(gamepad, &QGamepad::buttonStartChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Start" << pressed;
                this->buttonState("START", pressed);
            });
            connect(gamepad, &QGamepad::buttonGuideChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Guide" << pressed;
            });

            emit newDevice(usbDevice, buttonList, commands); // Let the UI know we have a new controller
            return;
        }
    }
    else if (!gamepad->isConnected()) {
        delete gamepad;
        gamepad = Q_NULLPTR;
    }


    handle = hid_open(0x0b33, 0x0020, NULL);
    if (!handle) {
        handle = hid_open(0x0b33, 0x0030, NULL);
        if (!handle) {
            handle = hid_open(0x0C26, 0x001E, NULL);
            if (!handle) {
                usbDevice = NONE;
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
        emit newDevice(usbDevice, buttonList, commands); // Let the UI know we have a new controller
        QTimer::singleShot(0, this, SLOT(runTimer()));
        return;
    }

    // No devices found, schedule another check in 1000ms
    QTimer::singleShot(1000, this, SLOT(run()));

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
            emit newDevice(0, buttonList, commands);
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



                // Step through all buttons and emit ones that have been pressed.
                for (unsigned char i = 0; i < 16; i++)
                {

                    for (BUTTON* but = buttonList->begin(); but != buttonList->end(); but++) {
                        if (but->dev == usbDevice && but->num == i) {

                            if ((tempButtons >> i & 1) && !(buttons >> i & 1) && but->onCommand->index > 0)
                            {
                                qInfo(logUsbControl()) << "On Button event:" << but->onCommand->text;
                                emit button(but->onCommand);
                            }
                            else if ((buttons >> i & 1) && !(tempButtons >> i & 1) && but->offCommand->index > 0)
                            {
                                qInfo(logUsbControl()) << "Off Button event:" << but->offCommand->text;
                                emit button(but->offCommand);
                            }
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

void usbController::buttonState(QString name, bool val)
{
    for (BUTTON* but = buttonList->begin(); but != buttonList->end(); but++) {
        if (but->dev == usbDevice && but->name == name) {

            if (val && but->onCommand->index > 0) {
                qInfo(logUsbControl()) << "On Button" << but->name << "event:" << but->onCommand->text;
                emit button(but->onCommand);
            }
            if (!val && but->offCommand->index > 0) {
                qInfo(logUsbControl()) << "Off Button" << but->name << "event:" << but->offCommand->text;
                emit button(but->offCommand);
            }
        }
    }
}
void usbController::buttonState(QString name, double val)
{
    for (BUTTON* but = buttonList->begin(); but != buttonList->end(); but++) {
        if (but->dev == usbDevice && but->name == name) {

            if (val && but->onCommand->index > 0) {
                qInfo(logUsbControl()) << "On Button" << but->name << "event:" << but->onCommand->text;
                emit button(but->onCommand);
            }
            if (!val && but->offCommand->index > 0) {
                qInfo(logUsbControl()) << "Off Button" << but->name << "event:" << but->offCommand->text;
                emit button(but->offCommand);
            }
        }
    }
}