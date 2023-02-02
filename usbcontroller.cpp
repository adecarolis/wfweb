
#if defined(USB_CONTROLLER)
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
    ledControl(false, 3);
    hid_close(handle);
    hid_exit();
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    if (gamepad != Q_NULLPTR)
    {
        delete gamepad;
        gamepad = Q_NULLPTR;
    }
#endif
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


void usbController::run()
{

    if (commands == Q_NULLPTR) {
        // We are not ready yet, commands haven't been loaded!
        QTimer::singleShot(1000, this, SLOT(run()));
        return;
    }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
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
            connect(gamepad, &QGamepad::buttonDownChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Down" << pressed;
                this->buttonState("DOWN", pressed);
            });
            connect(gamepad, &QGamepad::buttonUpChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Up" << pressed;
                this->buttonState("UP", pressed);
            });
            connect(gamepad, &QGamepad::buttonLeftChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Left" << pressed;
                this->buttonState("LEFT", pressed);
            });
            connect(gamepad, &QGamepad::buttonRightChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Right" << pressed;
                this->buttonState("RIGHT", pressed);
            });
            connect(gamepad, &QGamepad::buttonCenterChanged, this, [this](bool pressed) {
                qInfo(logUsbControl()) << "Button Center" << pressed;
                this->buttonState("CENTER", pressed);
            });
            connect(gamepad, &QGamepad::axisLeftXChanged, this, [this](double value) {
                qInfo(logUsbControl()) << "Left X" << value;
                this->buttonState("LEFTX", value);
            });
            connect(gamepad, &QGamepad::axisLeftYChanged, this, [this](double value) {
                qInfo(logUsbControl()) << "Left Y" << value;
                this->buttonState("LEFTY", value);
            });
            connect(gamepad, &QGamepad::axisRightXChanged, this, [this](double value) {
                qInfo(logUsbControl()) << "Right X" << value;
                this->buttonState("RIGHTX", value);
            });
            connect(gamepad, &QGamepad::axisRightYChanged, this, [this](double value) {
                qInfo(logUsbControl()) << "Right Y" << value;
                this->buttonState("RIGHTY", value);
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
            connect(gamepad, &QGamepad::buttonL2Changed, this, [this](double value) {
                qInfo(logUsbControl()) << "Button L2: " << value;
                this->buttonState("L2", value);
            });
            connect(gamepad, &QGamepad::buttonR2Changed, this, [this](double value) {
                qInfo(logUsbControl()) << "Button R2: " << value;
                this->buttonState("R2", value);
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
#endif

    handle = hid_open(0x0b33, 0x0020, NULL);
    if (!handle) {
        handle = hid_open(0x0b33, 0x0030, NULL);
        if (!handle) {
            handle = hid_open(0x0c26, 0x001e, NULL);
            if (!handle) {
                usbDevice = NONE;
            }
            else {
                usbDevice = RC28;
                getVersion();
                ledControl(false, 0);
                ledControl(false, 1);
                ledControl(false, 2);
                ledControl(true, 3);
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
        else if ((res > 31) && usbDevice == RC28)
        {
            // This is a response from the Icom RC28
            if ((unsigned char)data[0] == 0x02) {
                qInfo(logUsbControl()) << QString("Received RC-28 Firmware Version: %0").arg(QString(data.mid(1,data.indexOf(" ")-1)));
            }
            else 
            {

                data.resize(8);
                // Buttons

                BUTTON* butptt = Q_NULLPTR;
                BUTTON* butf1 = Q_NULLPTR;
                BUTTON* butf2 = Q_NULLPTR;;

                for (BUTTON* but = buttonList->begin(); but != buttonList->end(); but++) {
                    if (but->dev == usbDevice) {

                        if (but->num == 0)
                        {
                            butptt = but;
                        }
                        else if (but->num == 1)
                        {
                            butf1 = but;
                        }
                        else if (but->num == 2)
                        {
                            butf2 = but;
                        }
                    }
                }

                if (butptt != Q_NULLPTR && !((unsigned char)data[5] ^ 0x06) && ((unsigned char)lastData[5] ^ 0x06))
                {
                    // TRANSMIT key down only (no other keys down)
                    qDebug(logUsbControl()) << "PTT key down";
                    qInfo(logUsbControl()) << "On Button event:" << butptt->onCommand->text;
                    ledControl(true, 0);
                    emit button(butptt->onCommand);

                }
                else if (butptt != Q_NULLPTR && ((unsigned char)data[5] ^ 0x06) && !((unsigned char)lastData[5] ^ 0x06))
                {
                    // TRANSMIT key up only (no other keys down)
                    //emit button(false, 6);
                    qDebug(logUsbControl()) << "PTT key up";
                    qInfo(logUsbControl()) << "Off Button event:" << butptt->offCommand->text;
                    ledControl(false, 0);
                    emit button(butptt->offCommand);
                }
                
                if (butf1 != Q_NULLPTR && !((unsigned char)data[5] ^ 0x05) && ((unsigned char)lastData[5] ^ 0x05))
                {
                    // F-1 key up only (no other keys down)
                    //emit button(true, 5);
                    qDebug(logUsbControl()) << "F-1 key down";
                    qInfo(logUsbControl()) << "On Button event:" << butf1->onCommand->text;
                    ledControl(true, 1);
                    emit button(butf1->onCommand);
                }
                else if (butf1 != Q_NULLPTR && ((unsigned char)data[5] ^ 0x05) && !((unsigned char)lastData[5] ^ 0x05))
                {
                    // F-1 key down only (no other keys down)
                    //emit button(false, 5);
                    qDebug(logUsbControl()) << "F-1 key up";
                    qInfo(logUsbControl()) << "Off Button event:" << butf1->offCommand->text;
                    ledControl(false, 1);
                    emit button(butf1->offCommand);
                }
                
                if (butf2 != Q_NULLPTR && !((unsigned char)data[5] ^ 0x03) && ((unsigned char)lastData[5] ^ 0x03))
                {
                    // F-2 key up only (no other keys down)
                    //emit button(true, 7);
                    qDebug(logUsbControl()) << "F-2 key down";
                    qInfo(logUsbControl()) << "On Button event:" << butf2->onCommand->text;
                    ledControl(true, 2);
                    emit button(butf2->onCommand);
                }
                else if (butf2 != Q_NULLPTR && ((unsigned char)data[5] ^ 0x03) && !((unsigned char)lastData[5] ^ 0x03))
                {
                    // F-2 key down only (no other keys down)
                    //emit button(false, 7);
                    qDebug(logUsbControl()) << "F-2 key up";
                    qInfo(logUsbControl()) << "Off Button event:" << butf2->offCommand->text;
                    ledControl(false, 2);
                    emit button(butf2->offCommand);
                }

                if ((unsigned char)data[5] == 0x07)
                {
                    if ((unsigned char)data[3] == 0x01)
                    {
                        jogCounter = jogCounter + data[1];
                    }
                    else if ((unsigned char)data[3] == 0x02)
                    {
                        jogCounter = jogCounter - data[1];
                    }
                }

                lastData = data;
            }
        }

        if (lastusbController.msecsTo(QTime::currentTime()) >= 100 || lastusbController > QTime::currentTime())
        {
            if (usbDevice == shuttleXpress || usbDevice == shuttlePro2)
            {
                if (shutpos > 0  && shutpos < 0x08)
                {
                    shutMult = shutpos;
                    emit doShuttle(true, shutMult);
                    qDebug(logUsbControl()) << "Shuttle PLUS" << shutMult;

                }
                else if (shutpos > 0xEF) {
                    shutMult = abs(shutpos - 0xff) + 1;
                    emit doShuttle(false, shutMult);
                    qDebug(logUsbControl()) << "Shuttle MINUS" << shutMult;
                }
            }
            if (jogCounter != 0) {
                emit sendJog(jogCounter);
                qDebug(logUsbControl()) << "Change Frequency by" << jogCounter << "hz";
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
        QByteArray data(3, 0x0);
        data[1] = 0x01;
        static unsigned char ledNum = 0x07;
        if (on)
            ledNum &= ~(1UL << num);
        else
            ledNum |= 1UL << num;

        data[2] = ledNum;

        int res = hid_write(handle, (const unsigned char*)data.constData(), data.size());

        if (res < 0) {
            qDebug(logUsbControl()) << "Unable to write(), Error:" << hid_error(handle);
            return;
        }

        qDebug(logUsbControl()) << "write() success";
    }
}

void usbController::getVersion()
{
    QByteArray data(64, 0x0);
    data[0] = 63;
    data[1] = 0x02;
    int res = hid_write(handle, (const unsigned char*)data.constData(), data.size());

    if (res < 0) {
        qDebug(logUsbControl()) << "Unable to write(), Error:" << hid_error(handle);
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

    if (name == "LEFTX")
    {
        int value = val * 1000000;
        emit sendJog(value);
    }
    /*
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
    */
}
#endif
