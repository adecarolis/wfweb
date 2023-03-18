
#if defined(USB_CONTROLLER)
#include "usbcontroller.h"

#ifdef Q_OS_WIN
#pragma comment (lib, "Setupapi.lib")
#endif

#include <QDebug>

#include "logcategories.h"

usbController::usbController()
{
    // As this is run in it's own thread, don't do anything in the constructor
	qInfo(logUsbControl()) << "Starting usbController()";
    loadCommands();
    loadButtons();
    loadKnobs();
}

usbController::~usbController()
{
    qInfo(logUsbControl) << "Ending usbController()";

    for (USBDEVICE &dev: usbDevices)
    {
        if (dev.handle) {
            programOverlay(dev.path, 60, "Goodbye from wfview");

            if (dev.usbDevice == RC28) {
                ledControl(false, 3);
            }
            emit removeDevice(&dev);
            hid_close(dev.handle);
        }
    }

    hid_exit();
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    if (gamepad != Q_NULLPTR)
    {
        delete gamepad;
        gamepad = Q_NULLPTR;
    }
#endif
}

void usbController::init(QMutex* mut,usbMap* prefs ,QVector<BUTTON>* buts,QVector<KNOB>* knobs)
{
    mutex = mut;
    this->controllers = prefs;
    this->buttonList = buts;
    this->knobList = knobs;

    QMutexLocker locker(mutex);

    // We need to make sure that all buttons/knobs have a command assigned, this is a fairly expensive operation

    for (BUTTON* but = buttonList->begin(); but != buttonList->end(); but++)
    {
        QVector<COMMAND>::iterator usbc = commands.begin();

        while (usbc != commands.end())
        {
            if (but->on == usbc->text)
                but->onCommand = usbc;
            if (but->off == usbc->text)
                but->offCommand = usbc;
            ++usbc;
        }

    }

    for (KNOB* kb = knobList->begin(); kb != knobList->end(); kb++)
    {
        QVector<COMMAND>::iterator usbc = commands.begin();
        while (usbc != commands.end())
        {
            if (kb->cmd == usbc->text)
                kb->command = usbc;
            ++usbc;
        }
    }


#ifdef HID_API_VERSION_MAJOR
    if (HID_API_VERSION == HID_API_MAKE_VERSION(hid_version()->major, hid_version()->minor, hid_version()->patch)) {
        qInfo(logUsbControl) << QString("Compile-time version matches runtime version of hidapi: %0.%1.%2")
            .arg(hid_version()->major)
            .arg(hid_version()->minor)
            .arg(hid_version()->patch);
    }
    else {
        qInfo(logUsbControl) << QString("Compile-time and runtime versions of hidapi do not match (%0.%1.%2 vs %0.%1.%2)")
            .arg(HID_API_VERSION_MAJOR)
            .arg(HID_API_VERSION_MINOR)
            .arg(HID_API_VERSION_PATCH)
            .arg(hid_version()->major)
            .arg(hid_version()->minor)
            .arg(hid_version()->patch);
    }
#endif
    hidStatus = hid_init();
    if (hidStatus) {
        qInfo(logUsbControl()) << "Failed to intialize HID Devices";
    }
    else {

#ifdef HID_API_VERSION_MAJOR
#if defined(__APPLE__) && HID_API_VERSION >= HID_API_MAKE_VERSION(0, 12, 0)
        hid_darwin_set_open_exclusive(0);
#endif
#endif
        
        qInfo(logUsbControl()) << "Found available HID devices (not all will be suitable for use):";
        struct hid_device_info* devs;
        devs = hid_enumerate(0x0, 0x0);
        while (devs) {
            qInfo(logUsbControl()) << QString("Device found: (%0:%1) %2 manufacturer: (%3)%4 usage: 0x%5 usage_page 0x%6")
                .arg(devs->vendor_id, 4, 16, QChar('0'))
                .arg(devs->product_id, 4, 16, QChar('0'))
                .arg(QString::fromWCharArray(devs->product_string))
                .arg(QString::fromWCharArray(devs->product_string))
                .arg(QString::fromWCharArray(devs->manufacturer_string))
                .arg(devs->usage, 4, 16, QChar('0'))
                .arg(devs->usage_page, 4, 16, QChar('0'));
            devs = devs->next;
        }
        hid_free_enumeration(devs);
        
    }

}

/* run() is called every 2s and attempts to connect to a supported controller */
void usbController::run()
{
    QMutexLocker locker(mutex);

    if (hidStatus) {
        // We are not ready yet, hid hasn't been initialized!
        QTimer::singleShot(2000, this, SLOT(run()));
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

            USBDEVICE newDev;
            if (gamepad->name() == "Microsoft X-Box 360 pad 0")
            {
                newDev.usbDevice = xBoxGamepad;
            }
            else {
                newDev.usbDevice = unknownGamepad;
            }

            newDev.product = gamepad->name();
            newDev.path = gamepad->name();
            // Is this a new device? If so add it to usbDevices
            //auto p = std::find_if(usbDevices.begin(),usbDevices.end(),[newDev](const USBDEVICE& dev) {return dev.path == newDev.path; });
            //if (p == usbDevices.end()) {
            //    usbDevices.append(newDev);
            //}

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

            if (!controllers->contains(gamepad->name())) {
                controllers->insert(gamepad->name(),CONTROLLER());
            }

            newDev.connected=true;
            usbDevices.insert(newDev.path,newDev);
            emit newDevice(&newDev, &(*controllers)[gamepad->name()], buttonList, knobList, &commands, mutex); // Let the UI know we have a new controller
        }
    }
    else if (!gamepad->isConnected()) {
        delete gamepad;
        gamepad = Q_NULLPTR;
    }
#endif


#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    auto it = usbDevices.begin();
    while (it != usbDevices.end())
    {
        if (!it->connected)
            it = usbDevices.erase(it);
        else
            ++it;
    }
#else
    // Remove any devices from the list that are not connected (doesn't work on QT5!
    usbDevices.erase(std::remove_if(usbDevices.begin(), usbDevices.end(), [](const USBDEVICE& dev) { return (!dev.connected); }),usbDevices.end());
#endif
    struct hid_device_info* devs;
    devs = hid_enumerate(0x0, 0x0);

    while (devs) {
        for (int i = 0; i < (int)sizeof knownUsbDevices / (int)sizeof knownUsbDevices[0]; i++) {
            if (devs->vendor_id == knownUsbDevices[i][1] && devs->product_id == knownUsbDevices[i][2] 
                && (knownUsbDevices[i][3] == 0x00 || devs->usage == knownUsbDevices[i][3])
                && (knownUsbDevices[i][4] == 0x00 || devs->usage_page == knownUsbDevices[i][4])) 
            {
                USBDEVICE newDev;
                newDev.manufacturer = QString::fromWCharArray(devs->manufacturer_string);
                newDev.vendorId = devs->vendor_id;
                newDev.productId = devs->product_id;
                newDev.product = QString::fromWCharArray(devs->product_string);
                newDev.serial = QString::fromWCharArray(devs->serial_number);
                newDev.path = QString::fromLocal8Bit(devs->path);
                newDev.deviceId = QString("0x%1").arg(newDev.productId, 4, 16, QChar('0'));
                newDev.usbDevice = (usbDeviceType)knownUsbDevices[i][0];
                newDev.uuid = QUuid::createUuid();
                // Is this a new device? If so add it to usbDevices
                auto p = std::find_if(usbDevices.begin(),usbDevices.end(),[newDev](const USBDEVICE& dev) {return dev.path == newDev.path; });
                if (p == usbDevices.end()) {
                    usbDevices.insert(newDev.path,newDev);
                }
            }
        } 
        devs = devs->next;
    }

    hid_free_enumeration(devs);

    for (USBDEVICE &dev: usbDevices) {

        if (!dev.connected && !dev.path.isEmpty()) {
            qInfo(logUsbControl()) << QString("Attempting to connect to %0").arg(dev.product);
            dev.handle = hid_open_path(dev.path.toLocal8Bit());

            if (dev.handle)
            {
                qInfo(logUsbControl()) << QString("Connected to device: %0 from %1 S/N %2").arg(dev.product).arg(dev.manufacturer).arg(dev.serial);
                devicesConnected++;
                dev.connected=true;
                hid_set_nonblocking(dev.handle, 1);

                if (dev.usbDevice == shuttleXpress || dev.usbDevice == shuttlePro2)
                {
                    dev.knobValues.append(0);
                    dev.knobSend.append(0);
                }
                else if (dev.usbDevice == RC28) {
                    getVersion();
                    ledControl(false, 0);
                    ledControl(false, 1);
                    ledControl(false, 2);
                    ledControl(true, 3);
                    dev.knobValues.append(0);
                    dev.knobSend.append(0);
                }
                else if (dev.usbDevice == eCoderPlus)
                {
                    dev.knobValues.append({ 0,0,0,0 });
                    dev.knobSend.append({ 0,0,0,0 });
                }
                else if (dev.usbDevice == QuickKeys) {
                    // Subscribe to event streams

                    QByteArray b(32,0x0);
                    b[0] = (qint8)0x02;
                    b[1] = (qint8)0xb0;
                    b[2] = (qint8)0x04;

                    b.replace(10, sizeof(dev.deviceId), dev.deviceId.toLocal8Bit());
                    hid_write(dev.handle, (const unsigned char*)b.constData(), b.size());

                    b[0] = (qint8)0x02;
                    b[1] = (qint8)0xb4;
                    b[2] = (qint8)0x10;
                    b.replace(10, sizeof(dev.deviceId), dev.deviceId.toLocal8Bit());

                    hid_write(dev.handle, (const unsigned char*)b.constData(), b.size());
                    dev.knobValues.append(0);
                    dev.knobSend.append(0);
                }

                dev.knobPrevious.append(dev.knobValues);

                // Find our defaults/knobs/buttons for this controller:
                // First see if we have any stored and add them to the list if not.

                if (!controllers->contains(dev.path)) {
                    controllers->insert(dev.path,CONTROLLER());
                }

                auto bti = std::find_if(buttonList->begin(), buttonList->end(), [dev](const BUTTON& b)
                    { return (b.devicePath == dev.path); });
                if (bti == buttonList->end())
                {
                    // List doesn't contain any buttons for this device so add all existing buttons to the end of buttonList
                    qInfo(logUsbControl()) << "No stored buttons found, loading defaults";
                    for (BUTTON but :defaultButtons)
                    {
                        if (but.dev == dev.usbDevice)
                        {
                            but.devicePath = dev.path;
                            buttonList->append(but);
                        }
                    }

                }

                auto kbi = std::find_if(knobList->begin(), knobList->end(), [dev](const KNOB& k)
                    { return (k.devicePath == dev.path); });
                if (kbi == knobList->end())
                {
                    qInfo(logUsbControl()) << "No stored knobs found, loading defaults";
                    for (KNOB kb :defaultKnobs)
                    {
                        if (kb.dev == dev.usbDevice)
                        {
                            kb.devicePath = dev.path;
                            knobList->append(kb);
                        }
                    }
                }
                // Let the UI know we have a new controller
                emit newDevice(&dev, &(*controllers)[dev.path],buttonList, knobList, &commands, mutex);
            }
            else
            {

                // This should only get displayed once if we fail to connect to a device
                if (dev.usbDevice != usbNone)
                {
                    qInfo(logUsbControl()) << QString("Error connecting to  %0: %1")
                        .arg(dev.product)
                        .arg(QString::fromWCharArray(hid_error(dev.handle)));
                }
                // Call me again in 2 seconds to try connecting again
            }
        }
    }

    if (devicesConnected>0) {
        // Run the periodic timer to get data if we have any devices
        QTimer::singleShot(25, this, SLOT(runTimer()));
    }

    // Run the periodic timer to check for new devices
    QTimer::singleShot(2000, this, SLOT(run()));

}


/* runTimer is called every 25ms once a connection to a supported controller is established */
void usbController::runTimer()
{
    QMutexLocker locker(mutex);

    for (USBDEVICE &dev: usbDevices)
    {
        if (!dev.connected || !dev.handle) {
            continue;
        }

        int res=1;

        while (res > 0) {
            QByteArray data(HIDDATALENGTH, 0x0);
            res = hid_read(dev.handle, (unsigned char*)data.data(), HIDDATALENGTH);
            if (res < 0)
            {
                qInfo(logUsbControl()) << "USB Device disconnected" << dev.product;
                emit removeDevice(&dev);
                hid_close(dev.handle);
                dev.handle = NULL;
                dev.connected=false;
                devicesConnected--;
                break;
            }
            else if (res == 5 && (dev.usbDevice == shuttleXpress || dev.usbDevice == shuttlePro2))
            {
                data.resize(res);

                /*qDebug(logUsbControl()) << "usbController Data received " << hex << (unsigned char)data[0] << ":"
                    << hex << (unsigned char)data[1] << ":"
                    << hex << (unsigned char)data[2] << ":"
                    << hex << (unsigned char)data[3] << ":"
                    << hex << (unsigned char)data[4];
                    */
                quint32 tempButtons = ((quint8)data[4] << 8) | ((quint8)data[3] & 0xff);
                unsigned char tempJogpos = (unsigned char)data[1];
                unsigned char tempShutpos = (unsigned char)data[0];

                if (tempJogpos == dev.jogpos + 1 || (tempJogpos == 0 && dev.jogpos == 0xff))
                {
                    dev.knobValues[0]++;
                    //qDebug(logUsbControl()) << "JOG PLUS" << jogCounter;
                }
                else if (tempJogpos != dev.jogpos) {
                    dev.knobValues[0]--;
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
                if (dev.buttons != tempButtons)
                {

                    // Step through all buttons and emit ones that have been pressed.

                    for (unsigned char i = 0; i < 16; i++)
                    {
                        auto but = std::find_if(buttonList->begin(), buttonList->end(), [i,dev](const BUTTON& b)
                            { return (b.devicePath == dev.path && b.num == i); });
                        if (but != buttonList->end()) {
                            if ((tempButtons >> i & 1) && !(dev.buttons >> i & 1))
                            {
                                qDebug(logUsbControl()) << "On Button event:" << but->onCommand->text;
                                emit button(but->onCommand);
                            }
                            else if ((dev.buttons >> i & 1) && !(tempButtons >> i & 1))
                            {
                                qDebug(logUsbControl()) << "Off Button event:" << but->offCommand->text;
                                emit button(but->offCommand);
                            }
                        }
                    }
                }

                dev.buttons = tempButtons;
                dev.jogpos = tempJogpos;
                dev.shutpos = tempShutpos;

            }
            else if ((res > 31) && dev.usbDevice == RC28)
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
                        if (but->devicePath == dev.path) {

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

                    if (butptt != Q_NULLPTR && !((unsigned char)data[5] ^ 0x06) && ((unsigned char)dev.lastData[5] ^ 0x06))
                    {
                        // TRANSMIT key down only (no other keys down)
                        qDebug(logUsbControl()) << "PTT key down";
                        qInfo(logUsbControl()) << "On Button event:" << butptt->onCommand->text;
                        ledControl(true, 0);
                        emit button(butptt->onCommand);

                    }
                    else if (butptt != Q_NULLPTR && ((unsigned char)data[5] ^ 0x06) && !((unsigned char)dev.lastData[5] ^ 0x06))
                    {
                        // TRANSMIT key up only (no other keys down)
                        //emit button(false, 6);
                        qDebug(logUsbControl()) << "PTT key up";
                        qInfo(logUsbControl()) << "Off Button event:" << butptt->offCommand->text;
                        ledControl(false, 0);
                        emit button(butptt->offCommand);
                    }

                    if (butf1 != Q_NULLPTR && !((unsigned char)data[5] ^ 0x05) && ((unsigned char)dev.lastData[5] ^ 0x05))
                    {
                        // F-1 key up only (no other keys down)
                        //emit button(true, 5);
                        qDebug(logUsbControl()) << "F-1 key down";
                        qInfo(logUsbControl()) << "On Button event:" << butf1->onCommand->text;
                        ledControl(true, 1);
                        emit button(butf1->onCommand);
                    }
                    else if (butf1 != Q_NULLPTR && ((unsigned char)data[5] ^ 0x05) && !((unsigned char)dev.lastData[5] ^ 0x05))
                    {
                        // F-1 key down only (no other keys down)
                        //emit button(false, 5);
                        qDebug(logUsbControl()) << "F-1 key up";
                        qInfo(logUsbControl()) << "Off Button event:" << butf1->offCommand->text;
                        ledControl(false, 1);
                        emit button(butf1->offCommand);
                    }

                    if (butf2 != Q_NULLPTR && !((unsigned char)data[5] ^ 0x03) && ((unsigned char)dev.lastData[5] ^ 0x03))
                    {
                        // F-2 key up only (no other keys down)
                        //emit button(true, 7);
                        qDebug(logUsbControl()) << "F-2 key down";
                        qInfo(logUsbControl()) << "On Button event:" << butf2->onCommand->text;
                        ledControl(true, 2);
                        emit button(butf2->onCommand);
                    }
                    else if (butf2 != Q_NULLPTR && ((unsigned char)data[5] ^ 0x03) && !((unsigned char)dev.lastData[5] ^ 0x03))
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
                            dev.knobValues[0] = dev.knobValues[0] + data[1];
                        }
                        else if ((unsigned char)data[3] == 0x02)
                        {
                            dev.knobValues[0] = dev.knobValues[0] - data[1];
                        }
                    }

                    dev.lastData = data;
                }
            }
            else if (dev.usbDevice == eCoderPlus && data.length() > 0x0f && (quint8)data[0] == 0xff) {
                /* Button matrix:
                DATA3   DATA2     DATA 1
                765432107654321076543210
                001000000000000000000000 = Left CW
                000100000000000000000000 = Right CW
                000010000000000000000000 = PTT
                000001000000000000000000 = Knob 3 Press
                000000100000000000000000 = Knob 2 Press
                000000010000000000000000 = Knob 1 Press
                000000000100000000000000 = button14
                000000000010000000000000 = button13
                000000000001000000000000 = button12
                000000000000100000000000 = button11
                000000000000010000000000 = button10
                000000000000001000000000 = button9
                000000000000000100000000 = button8
                000000000000000010000000 = button7
                000000000000000001000000 = button6
                000000000000000000100000 = button5
                000000000000000000010000 = button4
                000000000000000000001000 = button3
                000000000000000000000100 = button2
                000000000000000000000010 = button1
                */
                quint32 tempButtons = ((quint8)data[3] << 16) | ((quint8)data[2] << 8) | ((quint8)data[1] & 0xff);
                quint32 tempKnobs = ((quint8)data[16] << 16) | ((quint8)data[15] << 8) | ((quint8)data[14] & 0xff);

                if (dev.buttons != tempButtons)
                {
                    // Step through all buttons and emit ones that have been pressed.
                    for (unsigned char i = 1; i < 23; i++)
                    {
                        auto but = std::find_if(buttonList->begin(), buttonList->end(), [dev, i](const BUTTON& b)
                        { return (b.devicePath == dev.path && b.num == i); });
                        if (but != buttonList->end()) {
                            if ((tempButtons >> i & 1) && !(dev.buttons >> i & 1))
                            {
                                qDebug(logUsbControl()) << "On Button event:" << but->onCommand->text;
                                emit button(but->onCommand);
                            }
                            else if ((dev.buttons >> i & 1) && !(tempButtons >> i & 1))
                            {
                                qDebug(logUsbControl()) << "Off Button event:" << but->offCommand->text;
                                emit button(but->offCommand);
                            }
                        }
                    }
                }
                dev.buttons = tempButtons;

                if (dev.knobs != tempKnobs) {
                    // One of the knobs has moved
                    for (unsigned char i = 0; i < 3; i++) {
                        if ((tempKnobs >> (i * 8) & 0xff) != (dev.knobs >> (i * 8) & 0xff)) {
                            dev.knobValues[i] = dev.knobValues[i] + (qint8)((dev.knobs >> (i * 8)) & 0xff);
                        }
                    }
                }
                dev.knobs = tempKnobs;

                // Tuning knob
                dev.knobValues[0] = dev.knobValues[0] + (qint8)data[13];

            } else if (dev.usbDevice == QuickKeys && (quint8)data[0] == 0x02) {

                if ((quint8)data[1] == 0xf0) {

                    //qInfo(logUsbControl()) << "Received:" << data;
                    quint32 tempButtons = (data[3] << 8) | (data[2] & 0xff);

                    // Step through all buttons and emit ones that have been pressed.
                    for (unsigned char i = 0; i < 10; i++)
                    {
                        auto but = std::find_if(buttonList->begin(), buttonList->end(), [dev, i](const BUTTON& b)
                        { return (b.devicePath == dev.path && b.num == i); });
                        if (but != buttonList->end()) {
                            if ((tempButtons >> i & 1) && !(dev.buttons >> i & 1))
                            {
                                qDebug(logUsbControl()) << "On Button event:" << but->onCommand->text;
                                emit button(but->onCommand);
                                programButton(dev.path, but->num, but->offCommand->text);
                            }
                            else if ((dev.buttons >> i & 1) && !(tempButtons >> i & 1))
                            {
                                qDebug(logUsbControl()) << "Off Button event:" << but->offCommand->text;
                                emit button(but->offCommand);
                                programButton(dev.path, but->num, but->onCommand->text);
                            }
                        }
                    }

                    dev.buttons = tempButtons;

                    // Tuning knob
                    if (data[7] & 0x01) {
                        dev.knobValues[0]++;
                    }
                    else if (data[7] & 0x02) {
                        dev.knobValues[0]--;
                    }
                }
                else if ((quint8)data[1] == 0xf2 && (quint8)data[2] == 0x01)
                {
                    // Battery level
                    quint8 battery = (quint8)data[3];
                    qDebug(logUsbControl()) << QString("Battery level %1 %").arg(battery);
                }
            }

            if (dev.lastusbController.msecsTo(QTime::currentTime()) >= 100 || dev.lastusbController > QTime::currentTime())
            {
                if (dev.usbDevice == shuttleXpress || dev.usbDevice == shuttlePro2)
                {
                    if (dev.shutpos > 0  && dev.shutpos < 0x08)
                    {
                        dev.shutMult = dev.shutpos;
                        emit doShuttle(true, dev.shutMult);
                        qDebug(logUsbControl()) << "Shuttle PLUS" << dev.shutMult;

                    }
                    else if (dev.shutpos > 0xEF) {
                        dev.shutMult = abs(dev.shutpos - 0xff) + 1;
                        emit doShuttle(false, dev.shutMult);
                        qDebug(logUsbControl()) << "Shuttle MINUS" << dev.shutMult;
                    }
                }

                for (unsigned char i = 0; i < dev.knobValues.size(); i++) {
                    for (KNOB* kb = knobList->begin(); kb != knobList->end(); kb++) {
                        if (kb != knobList->end() && kb->command && kb->devicePath == dev.path && kb->num == i && dev.knobValues[i]) {
                            // sendCommand mustn't be deleted so we ensure it stays in-scope by declaring it private.
                            sendCommand = *kb->command;
                            if (kb->num >0) {
                                if (dev.knobSend[i] + (dev.knobValues[i] * 10) <= 0)
                                {
                                    dev.knobSend[i] = 0;
                                }
                                else if (dev.knobSend[i] + (dev.knobValues[i] * 10) >= 255)
                                {
                                    dev.knobSend[i] = 255;
                                }
                                else {
                                    dev.knobSend[i] = dev.knobSend[i] + (dev.knobValues[i] * 10);
                                }
                                sendCommand.suffix = dev.knobSend[i];
                            } else {
                                int tempVal = dev.knobValues[i] * dev.sensitivity;
                                tempVal = qMin(qMax(tempVal,0),255);
                                sendCommand.suffix = quint8(tempVal);
                                dev.knobValues[i]=tempVal/dev.sensitivity; // This ensures that dial can't go outside 0-255
                            }

                            if (dev.knobValues[i] != dev.knobPrevious[i]) {
                                emit button(&sendCommand);
                            }

                            if (sendCommand.command == cmdSetFreq) {
                                dev.knobValues[i] = 0;
                            } else {
                                dev.knobPrevious[i]=dev.knobValues[i];
                            }
                        }
                    }
                }

                /*
                if (dev.jogCounter != 0) {
                    emit sendJog(dev.jogCounter/dev.sensitivity);
                    qDebug(logUsbControl()) << "Change Frequency by" << dev.jogCounter << "hz";
                }
                */
                dev.lastusbController = QTime::currentTime();
            }

        }
    }

    // Run every 25ms
    QTimer::singleShot(25, this, SLOT(runTimer()));
}

void usbController::receivePTTStatus(bool on) {
    static QColor lastColour = currentColour;
    static bool ptt;

    for (USBDEVICE &dev: usbDevices) {
        if (on && !ptt) {
            lastColour = currentColour;
            programWheelColour(dev.path, 255, 0, 0);
        }
        else {
            programWheelColour(dev.path, (quint8)lastColour.red(), (quint8)lastColour.green(), (quint8)lastColour.blue());
        }
    }

    ptt = on;
}

/*
 * All functions below here are for specific controllers
*/

/* Functions below are for RC28 */

void usbController::ledControl(bool on, unsigned char num)
{
    for (USBDEVICE &dev: usbDevices) {
        if (dev.usbDevice == RC28) {
            QByteArray data(3, 0x0);
            data[1] = 0x01;
            static unsigned char ledNum = 0x07;
            if (on)
                ledNum &= ~(1UL << num);
            else
                ledNum |= 1UL << num;

            data[2] = ledNum;

            int res = hid_write(dev.handle, (const unsigned char*)data.constData(), data.size());

            if (res < 0) {
                qDebug(logUsbControl()) << "Unable to write(), Error:" << hid_error(dev.handle);
                return;
            }

            qDebug(logUsbControl()) << "write() success";
        }
    }
}

void usbController::getVersion()
{
    for (USBDEVICE &dev: usbDevices) {
        if (dev.usbDevice == RC28) {

            QByteArray data(64, 0x0);
            data[0] = 63;
            data[1] = 0x02;
            int res = hid_write(dev.handle, (const unsigned char*)data.constData(), data.size());

            if (res < 0) {
                qDebug(logUsbControl()) << "Unable to write(), Error:" << hid_error(dev.handle);
            }
        }
    }
}

/* End of RC28 functions*/

/* Functions below are for Gamepad controllers */
void usbController::buttonState(QString name, bool val)
{
    // Need to fix gamepad support
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

/* End of Gamepad functions*/


/* Functions below are for Xencelabs QuickKeys*/
void usbController::programButton(QString path, quint8 val, QString text)
{
    if (usbDevices.contains(path))
    {
        USBDEVICE* dev=&usbDevices[path];
        if (dev->usbDevice == QuickKeys && dev->handle && val < 8) {
            text = text.mid(0, 10); // Make sure text is no more than 10 characters.
            qDebug(logUsbControl()) << QString("Programming button %0 with %1").arg(val).arg(text);
            QByteArray data(32, 0x0);
            data[0] = (qint8)0x02;
            data[1] = (qint8)0xb1;
            data[3] = val + 1;
            data[5] = text.length() * 2;
            data.replace(10, dev->deviceId.size(), dev->deviceId.toLocal8Bit());

            QByteArray le = qToLittleEndian(QByteArray::fromRawData(reinterpret_cast<const char*>(text.constData()), text.size() * 2));
            data.replace(16, le.size(), le);

            int res = hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());

            if (res < 0) {
                qDebug(logUsbControl()) << "Unable to write(), Error:" << hid_error(dev->handle);
                return;
            }
        }
    }
}

void usbController::programSensitivity(QString path, quint8 val) {

    if (usbDevices.contains(path))
    {
        usbDevices[path].sensitivity=val;
    }
    (*controllers)[path].sensitivity = val;

}
void usbController::programBrightness(QString path, quint8 val) {

    if (usbDevices.contains(path))
    {
        USBDEVICE* dev = &usbDevices[path];
        if (dev->usbDevice == QuickKeys && dev->handle) {
            qDebug(logUsbControl()) << QString("Programming brightness to %0").arg(val+1);
            QByteArray data(32, 0x0);
            data[0] = (qint8)0x02;
            data[1] = (qint8)0xb1;
            data[2] = (qint8)0x0a;
            data[3] = (qint8)0x01;
            data[4] = val;
            data.replace(10, dev->deviceId.size(), dev->deviceId.toLocal8Bit());
            hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
        }
    }
    (*controllers)[path].brightness = val;

}

void usbController::programOrientation(QString path, quint8 val) {

    if (usbDevices.contains(path))
    {
        USBDEVICE* dev = &usbDevices[path];
        if (dev->usbDevice == QuickKeys && dev->handle) {
            qDebug(logUsbControl()) << QString("Programming orientation to %0").arg(val+1);
            QByteArray data(32, 0x0);
            data[0] = (qint8)0x02;
            data[1] = (qint8)0xb1;
            data[2] = (qint8)val+1;
            data.replace(10, dev->deviceId.size(), dev->deviceId.toLocal8Bit());
            hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
        }
    }
    (*controllers)[path].orientation = val;

}

void usbController::programSpeed(QString path, quint8 val) {

    if (usbDevices.contains(path))
    {
        USBDEVICE* dev = &usbDevices[path];
        if (dev->usbDevice == QuickKeys && dev->handle) {
            qDebug(logUsbControl()) << QString("Programming speed to %0").arg(val+1);
            QByteArray data(32, 0x0);
            data[0] = (qint8)0x02;
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x04;
            data[3] = (qint8)0x01;
            data[4] = (qint8)0x01;
            data[5] = val+1;
            data.replace(10, dev->deviceId.size(), dev->deviceId.toLocal8Bit());
            hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
        }
        (*controllers)[path].speed = val;

    }
}


void usbController::programWheelColour(QString path, quint8 r, quint8 g, quint8 b)
{

    if (usbDevices.contains(path))
    {
        USBDEVICE* dev = &usbDevices[path];
        if (dev->usbDevice == QuickKeys && dev->handle) {
            QByteArray data(32, 0x0);
            data[0] = (qint8)0x02;
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x01;
            data[3] = (qint8)0x01;
            data[6] = (qint8)r;
            data[7] = (qint8)g;
            data[8] = (qint8)b;
            data.replace(10, dev->deviceId.size(), dev->deviceId.toLocal8Bit());
            hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
            currentColour.setRed(r);
            currentColour.setGreen(g);
            currentColour.setBlue(b);
        }
        (*controllers)[path].color=QColor(r,g,b);
    }
}
void usbController::programOverlay(QString path, quint8 duration, QString text)
{
    if (usbDevices.contains(path))
    {
        USBDEVICE* dev = &usbDevices[path];
        if (dev->usbDevice == QuickKeys && dev->handle) {
            text = text.mid(0, 32);
            QByteArray data(32, 0x0);
            data[0] = (qint8)0x02;
            data[1] = (qint8)0xb1;
            data[3] = (qint8)duration;
            data.replace(10, dev->deviceId.size(), dev->deviceId.toLocal8Bit());
            for (int i = 0; i < text.length(); i = i + 8)
            {
                data[2] = (i == 0) ? 0x05 : 0x06;
                QByteArray le = qToLittleEndian(QByteArray::fromRawData(reinterpret_cast<const char*>(text.mid(i, 8).constData()), text.mid(i, 8).size() * 2));
                data.replace(16, le.size(), le);
                data[5] = text.mid(i, 8).length() * 2;
                data[6] = (i > 0 && text.mid(i).size() > 8) ? 0x01 : 0x00;
                hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
            }
            //qInfo(logUsbControl()) << "Sent overlay" << text;
        }
    }
}

void usbController::programTimeout(QString path, quint8 val)
{
    if (usbDevices.contains(path))
    {
        USBDEVICE* dev = &usbDevices[path];
        if (dev->usbDevice == QuickKeys && dev->handle && path == dev->path) {
            qInfo(logUsbControl()) << QString("Programming %0 timeout to %1 minutes").arg(dev->product).arg(val);
            QByteArray data(32, 0x0);
            data[0] = (qint8)0x02;
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x08;
            data[3] = (qint8)0x01;
            data[4] = val;
            data.replace(10, dev->deviceId.size(), dev->deviceId.toLocal8Bit());
            hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
        }
        (*controllers)[path].timeout = val;
    }
}

/* End of functions for Xencelabs QuickKeys*/


/* Button/Knob/Command defaults */

void usbController::loadButtons()
{
    defaultButtons.clear();

    // ShuttleXpress
    defaultButtons.append(BUTTON(shuttleXpress, 4, QRect(25, 199, 89, 169), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttleXpress, 5, QRect(101, 72, 83, 88), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttleXpress, 6, QRect(238, 26, 134, 69), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttleXpress, 7, QRect(452, 72, 77, 86), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttleXpress, 8, QRect(504, 199, 89, 169), Qt::red, &commands[0], &commands[0]));

    // ShuttlePro2
    defaultButtons.append(BUTTON(shuttlePro2, 0, QRect(60, 66, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 1, QRect(114, 50, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 2, QRect(169, 47, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 3, QRect(225, 59, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 4, QRect(41, 132, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 5, QRect(91, 105, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 6, QRect(144, 93, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 7, QRect(204, 99, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 8, QRect(253, 124, 40, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 9, QRect(50, 270, 70, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 10, QRect(210, 270, 70, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 11, QRect(50, 335, 70, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 12, QRect(210, 335, 70, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 13, QRect(30, 195, 25, 80), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(shuttlePro2, 14, QRect(280, 195, 25, 80), Qt::red, &commands[0], &commands[0]));

    // RC28
    defaultButtons.append(BUTTON(RC28, 0, QRect(52, 445, 238, 64), Qt::red, &commands[1], &commands[2])); // PTT On/OFF
    defaultButtons.append(BUTTON(RC28, 1, QRect(52, 373, 98, 46), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(RC28, 2, QRect(193, 373, 98, 46), Qt::red, &commands[0], &commands[0]));

    // Xbox Gamepad
    defaultButtons.append(BUTTON(xBoxGamepad, "UP", QRect(256, 229, 50, 50), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "DOWN", QRect(256, 316, 50, 50), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "LEFT", QRect(203, 273, 50, 50), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "RIGHT", QRect(303, 273, 50, 50), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "SELECT", QRect(302, 160, 40, 40), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "START", QRect(412, 163, 40, 40), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "Y", QRect(534, 104, 53, 53), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "X", QRect(485, 152, 53, 53), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "B", QRect(590, 152, 53, 53), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "A", QRect(534, 202, 53, 53), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "L1", QRect(123, 40, 70, 45), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "R1", QRect(562, 40, 70, 45), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "LEFTX", QRect(143, 119, 83, 35), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "LEFTY", QRect(162, 132, 50, 57), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "RIGHTX", QRect(430, 298, 83, 35), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(xBoxGamepad, "RIGHTY", QRect(453, 233, 50, 57), Qt::red, &commands[0], &commands[0]));

    // eCoder
    defaultButtons.append(BUTTON(eCoderPlus, 1, QRect(87, 190, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 2, QRect(168, 190, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 3, QRect(249, 190, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 4, QRect(329, 190, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 5, QRect(410, 190, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 6, QRect(87, 270, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 7, QRect(168, 270, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 8, QRect(249, 270, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 9, QRect(329, 270, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 10, QRect(410, 270, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 11, QRect(87, 351, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 12, QRect(410, 351, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 13, QRect(87, 512, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 14, QRect(410, 512, 55, 55), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 16, QRect(128, 104, 45, 47), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 17, QRect(256, 104, 45, 47), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 18, QRect(380, 104, 45, 47), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 19, QRect(124, 2, 55, 30), Qt::red, &commands[1], &commands[2]));
    defaultButtons.append(BUTTON(eCoderPlus, 20, QRect(290, 2, 55, 30), Qt::red, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(eCoderPlus, 21, QRect(404, 2, 55, 30), Qt::red, &commands[0], &commands[0]));

    // QuickKeys
    defaultButtons.append(BUTTON(QuickKeys, 0, QRect(77, 204, 39, 63), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 1, QRect(77, 276, 39, 63), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 2, QRect(77, 348, 39, 63), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 3, QRect(77, 422, 39, 63), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 4, QRect(230, 204, 39, 63), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 5, QRect(230, 276, 39, 63), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 6, QRect(230, 348, 39, 63), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 7, QRect(230, 422, 39, 63), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 8, QRect(143, 515, 55, 40), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(QuickKeys, 9, QRect(139, 68, 65, 65), Qt::white, &commands[0], &commands[0]));
}

void usbController::loadKnobs()
{
    defaultKnobs.clear();
    defaultKnobs.append(KNOB(shuttleXpress, 0, QRect(205, 189, 203, 203), Qt::green, &commands[4]));
    defaultKnobs.append(KNOB(shuttlePro2, 0, QRect(104, 164, 124, 119), Qt::green, &commands[4]));
    defaultKnobs.append(KNOB(RC28, 0, QRect(78, 128, 184, 168), Qt::green, &commands[4]));
    defaultKnobs.append(KNOB(QuickKeys, 0, QRect(114, 130, 121, 43), Qt::green, &commands[4]));

    // eCoder
    defaultKnobs.append(KNOB(eCoderPlus, 0, QRect(173, 360, 205, 209), Qt::green, &commands[4]));
    defaultKnobs.append(KNOB(eCoderPlus, 1, QRect(120, 153, 72, 27), Qt::green, &commands[0]));
    defaultKnobs.append(KNOB(eCoderPlus, 2, QRect(242, 153, 72, 27), Qt::green, &commands[0]));
    defaultKnobs.append(KNOB(eCoderPlus, 3, QRect(362, 153, 72, 27), Qt::green, &commands[0]));
}

void usbController::loadCommands()
{
    commands.clear();
    int num = 0;
    // Important commands at the top!
    commands.append(COMMAND(num++, "None", commandButton, cmdNone, 0x0));
    commands.append(COMMAND(num++, "PTT On", commandButton, cmdSetPTT, 0x1));
    commands.append(COMMAND(num++, "PTT Off", commandButton, cmdSetPTT, 0x0));
    commands.append(COMMAND(num++, "PTT Toggle", commandButton, cmdPTTToggle, 0x0));
    commands.append(COMMAND(num++, "VFOA", commandKnob, cmdSetFreq, 0x0));
    commands.append(COMMAND(num++, "VFOB", commandKnob, cmdSetFreq, 0x1));
    commands.append(COMMAND(num++, "Tune", commandButton, cmdStartATU, 0x0));
    commands.append(COMMAND(num++, "Step+", commandButton, cmdSetStepUp, 0x0));
    commands.append(COMMAND(num++, "Step-", commandButton, cmdSetStepDown, 0x0));
    commands.append(COMMAND(num++, "Span+", commandButton, cmdSetSpanUp, 0x0));
    commands.append(COMMAND(num++, "Span-", commandButton, cmdSetSpanDown, 0x0));
    commands.append(COMMAND(num++, "Mode+", commandButton, cmdSetModeUp, 0x0));
    commands.append(COMMAND(num++, "Mode-", commandButton, cmdSetModeDown, 0x0));
    commands.append(COMMAND(num++, "Mode LSB", commandButton, cmdSetMode, modeLSB));
    commands.append(COMMAND(num++, "Mode USB", commandButton, cmdSetMode, modeUSB));
    commands.append(COMMAND(num++, "Mode LSBD", commandButton, cmdSetMode, modeLSB_D));
    commands.append(COMMAND(num++, "Mode USBD", commandButton, cmdSetMode, modeUSB_D));
    commands.append(COMMAND(num++, "Mode CW", commandButton, cmdSetMode, modeCW));
    commands.append(COMMAND(num++, "Mode CWR", commandButton, cmdSetMode, modeCW_R));
    commands.append(COMMAND(num++, "Mode FM", commandButton, cmdSetMode, modeFM));
    commands.append(COMMAND(num++, "Mode AM", commandButton, cmdSetMode, modeAM));
    commands.append(COMMAND(num++, "Mode RTTY", commandButton, cmdSetMode, modeRTTY));
    commands.append(COMMAND(num++, "Mode RTTYR", commandButton, cmdSetMode, modeRTTY_R));
    commands.append(COMMAND(num++, "Mode PSK", commandButton, cmdSetMode, modePSK));
    commands.append(COMMAND(num++, "Mode PSKR", commandButton, cmdSetMode, modePSK_R));
    commands.append(COMMAND(num++, "Mode DV", commandButton, cmdSetMode, modeDV));
    commands.append(COMMAND(num++, "Mode DD", commandButton, cmdSetMode, modeDD));
    commands.append(COMMAND(num++, "Band+", commandButton, cmdSetBandUp, 0x0));
    commands.append(COMMAND(num++, "Band-", commandButton, cmdSetBandDown, 0x0));
    commands.append(COMMAND(num++, "Band 23cm", commandButton, cmdGetBandStackReg, band23cm));
    commands.append(COMMAND(num++, "Band 70cm", commandButton, cmdGetBandStackReg, band70cm));
    commands.append(COMMAND(num++, "Band 2m", commandButton, cmdGetBandStackReg, band2m));
    commands.append(COMMAND(num++, "Band AIR", commandButton, cmdGetBandStackReg, bandAir));
    commands.append(COMMAND(num++, "Band WFM", commandButton, cmdGetBandStackReg, bandWFM));
    commands.append(COMMAND(num++, "Band 4m", commandButton, cmdGetBandStackReg, band4m));
    commands.append(COMMAND(num++, "Band 6m", commandButton, cmdGetBandStackReg, band6m));
    commands.append(COMMAND(num++, "Band 10m", commandButton, cmdGetBandStackReg, band10m));
    commands.append(COMMAND(num++, "Band 12m", commandButton, cmdGetBandStackReg, band12m));
    commands.append(COMMAND(num++, "Band 15m", commandButton, cmdGetBandStackReg, band15m));
    commands.append(COMMAND(num++, "Band 17m", commandButton, cmdGetBandStackReg, band17m));
    commands.append(COMMAND(num++, "Band 20m", commandButton, cmdGetBandStackReg, band20m));
    commands.append(COMMAND(num++, "Band 30m", commandButton, cmdGetBandStackReg, band30m));
    commands.append(COMMAND(num++, "Band 40m", commandButton, cmdGetBandStackReg, band40m));
    commands.append(COMMAND(num++, "Band 60m", commandButton, cmdGetBandStackReg, band60m));
    commands.append(COMMAND(num++, "Band 80m", commandButton, cmdGetBandStackReg, band80m));
    commands.append(COMMAND(num++, "Band 160m", commandButton, cmdGetBandStackReg, band160m));
    commands.append(COMMAND(num++, "Band 630m", commandButton, cmdGetBandStackReg, band630m));
    commands.append(COMMAND(num++, "Band 2200m", commandButton, cmdGetBandStackReg, band2200m));
    commands.append(COMMAND(num++, "Band GEN", commandButton, cmdGetBandStackReg, bandGen));
    commands.append(COMMAND(num++, "NR On", commandButton, cmdNone, 0x0));
    commands.append(COMMAND(num++, "NR Off", commandButton, cmdNone, 0x0));
    commands.append(COMMAND(num++, "NB On", commandButton, cmdNone, 0x0));
    commands.append(COMMAND(num++, "NB Off", commandButton, cmdNone, 0x0));
    commands.append(COMMAND(num++, "Split On", commandButton, cmdNone, 0x01));
    commands.append(COMMAND(num++, "Split Off", commandButton, cmdNone, 0x0));
    commands.append(COMMAND(num++, "Swap VFO", commandButton, cmdVFOSwap, 0x0));
    commands.append(COMMAND(num++, "AF Gain", commandKnob, cmdSetAfGain, 0xff));
    commands.append(COMMAND(num++, "RF Gain", commandKnob, cmdSetRxRfGain, 0xff));
    commands.append(COMMAND(num++, "TX Power", commandKnob, cmdSetTxPower, 0xff));
    commands.append(COMMAND(num++, "Mic Gain", commandKnob, cmdSetMicGain, 0xff));
    commands.append(COMMAND(num++, "Mod Level", commandKnob, cmdSetModLevel, 0xff));
    commands.append(COMMAND(num++, "Squelch", commandKnob, cmdSetSql, 0xff));
    commands.append(COMMAND(num++, "IF Shift", commandKnob, cmdSetIFShift, 0xff));
    commands.append(COMMAND(num++, "In PBT", commandKnob, cmdSetTPBFInner, 0xff));
    commands.append(COMMAND(num++, "Out PBT", commandKnob, cmdSetTPBFOuter, 0xff));
    commands.append(COMMAND(num++, "CW Pitch", commandKnob, cmdSetCwPitch, 0xff));
    commands.append(COMMAND(num++, "CW Speed", commandKnob, cmdSetKeySpeed, 0xff));
}

#endif
