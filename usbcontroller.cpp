#if defined(USB_CONTROLLER)
#include "usbcontroller.h"

#ifdef Q_OS_WIN
#pragma comment (lib, "Setupapi.lib")
#endif

#include <QDebug>

#include "logcategories.h"

usbController::usbController()
{
    // As this is run in it's own thread, don't do anything important in the constructor.
    qInfo(logUsbControl()) << "Starting usbController()";
    loadCommands();
    loadButtons();
    loadKnobs();

    // This is a the "master" list of supported devices. Maybe move to settings at some point?
    // usbDeviceType, manufacturer, product, usage, usagePage, buttons, columns, knobs, leds, maxPayload, iconSize
    knownDevices.append(USBTYPE(shuttleXpress,0x0b33,0x0020,0x0000,0x0000,15,0,2,0,5,0));
    knownDevices.append(USBTYPE(shuttlePro2,0x0b33,0x0030,0x0000,0x0000,15,0,2,0,5,0));
    knownDevices.append(USBTYPE(shuttlePro2,0x0b33,0x0011,0x0000,0x0000,15,0,2,0,5,0)); // Actually a ShuttlePro but hopefully will work?
    knownDevices.append(USBTYPE(RC28,0x0c26,0x001e,0x0000,0x0000,3,0,1,3,64,0));
    knownDevices.append(USBTYPE(eCoderPlus, 0x1fc9, 0x0003,0x0000,0x0000,22,0,4,0,32,0)); // Actually 20 but some bit0 and bit15 aren't used
    knownDevices.append(USBTYPE(QuickKeys, 0x28bd, 0x5202,0x0001,0xff0a,10,0,1,0,32,0));
    knownDevices.append(USBTYPE(StreamDeckMini, 0x0fd9, 0x0063, 0x0000, 0x0000,6,0,0,0,1024,80));
    knownDevices.append(USBTYPE(StreamDeckMiniV2, 0x0fd9, 0x0090, 0x0000, 0x0000,6,0,0,0,1024,80));
    knownDevices.append(USBTYPE(StreamDeckOriginal, 0x0fd9, 0x0060, 0x0000, 0x0000,15,5,0,0,8191,72));
    knownDevices.append(USBTYPE(StreamDeckOriginalV2, 0x0fd9, 0x006d, 0x0000, 0x0000,15,5,0,0,1024,72));
    knownDevices.append(USBTYPE(StreamDeckOriginalMK2, 0x0fd9, 0x0080, 0x0000, 0x0000,15,5,0,0,1024,72));
    knownDevices.append(USBTYPE(StreamDeckXL, 0x0fd9, 0x006c, 0x0000, 0x0000,32,8,0,0,1024,96));
    knownDevices.append(USBTYPE(StreamDeckXLV2, 0x0fd9, 0x008f, 0x0000, 0x0000,32,8,0,0,1024,96));
    knownDevices.append(USBTYPE(StreamDeckPedal, 0x0fd9, 0x0086, 0x0000, 0x0000,3,0,0,0,1024,0));
    knownDevices.append(USBTYPE(StreamDeckPlus, 0x0fd9, 0x0084, 0x0000, 0x0000,12,0,4,0,1024,120));
    knownDevices.append(USBTYPE(XKeysXK3, 0x05f3, 0x04c5, 0x0001, 0x000c,3,0,0,2,32,0)); // So-called "splat" interface?
}

usbController::~usbController()
{
    qInfo(logUsbControl) << "Ending usbController()";
    auto devIt = devices->begin();
    while (devIt != devices->end())
    {
        auto dev = &devIt.value();
        if (dev->handle) {
            sendRequest(dev,usbFeatureType::featureOverlay,60,"Goodbye from wfview");

            if (dev->type.model == RC28) {
                sendRequest(dev,usbFeatureType::featureLEDControl,4,"0");
            }
            hid_close(dev->handle);
            dev->handle = NULL;
            dev->connected = false;
            dev->detected = false;
            dev->uiCreated = false;
            devicesConnected--;
        }
        ++devIt;
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

void usbController::init(QMutex* mut,usbDevMap* devs ,QVector<BUTTON>* buts,QVector<KNOB>* knobs)
{
    mutex = mut;
    this->devices = devs;
    this->buttonList = buts;
    this->knobList = knobs;
    
    emit initUI(this->devices, this->buttonList, this->knobList,&this->commands,this->mutex);
    
    QMutexLocker locker(mutex);
    
    // We need to make sure that all buttons/knobs have a command assigned, this is a fairly expensive operation
    // Perform a deep copy of the command to ensure that each controller is using a unique copy of the command.
    
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
                                      .arg(QString::fromWCharArray(devs->product_string),QString::fromWCharArray(devs->product_string),QString::fromWCharArray(devs->manufacturer_string))
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

#ifdef USB_HOTPLUG
   qDebug(logUsbControl()) << "Re-enumerating USB devices due to program startup or hotplug event";
#endif

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
                newDev.type.model = xBoxGamepad;
            }
            else {
                newDev.type.model = unknownGamepad;
            }
            
            newDev.product = gamepad->name();
            newDev.path = gamepad->name();
            
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

            newDev.connected=true;
            devices->insert(newDev.path,newDev);

            emit newDevice(&newDev); // Let the UI know we have a new controller
        }
    }
    else if (!gamepad->isConnected()) {
        delete gamepad;
        gamepad = Q_NULLPTR;
    }
#endif

    struct hid_device_info* devs;
    devs = hid_enumerate(0x0, 0x0);
    // Step through all currenly connected devices and add any newly discovered ones to usbDevices.
    while (devs) {
        auto i = std::find_if(knownDevices.begin(), knownDevices.end(), [devs](const USBTYPE& d)
        { return ((devs->vendor_id == d.manufacturerId) && (devs->product_id == d.productId)
                        && (d.usage == 0x00 || devs->usage == d.usage)
                        && (d.usagePage == 0x00 || devs->usage_page == d.usagePage));});

        if (i != knownDevices.end())
        {
            auto it = devices->find(QString::fromLocal8Bit(devs->path));
            if (it == devices->end())
            {
                USBDEVICE newDev(*i);
                newDev.manufacturer = QString::fromWCharArray(devs->manufacturer_string);
                newDev.product = QString::fromWCharArray(devs->product_string);
                if (newDev.product.isEmpty())
                {
                  newDev.product = "<Not Detected>";
                }
                newDev.serial = QString::fromWCharArray(devs->serial_number);
                newDev.path = QString::fromLocal8Bit(devs->path);
                newDev.deviceId = QString("0x%1").arg(newDev.type.productId, 4, 16, QChar('0'));
                newDev.detected = true;
                qDebug(logUsbControl()) << "New device detected" << newDev.product;
                devices->insert(newDev.path,newDev);
            } else if (!it->detected){
                // This is a known device
                auto dev = &it.value();
                dev->type = *i;
                dev->manufacturer = QString::fromWCharArray(devs->manufacturer_string);
                dev->product = QString::fromWCharArray(devs->product_string);
                if (dev->product.isEmpty())
                {
                  dev->product = "<Not Detected>";
                }
                dev->serial = QString::fromWCharArray(devs->serial_number);
                dev->deviceId = QString("0x%1").arg(dev->type.productId, 4, 16, QChar('0'));
                dev->detected = true;
                qDebug(logUsbControl()) << "Known device detected" << dev->product;
            }
        }
        devs = devs->next;
    }
    
    hid_free_enumeration(devs);
    
    for (auto devIt = devices->begin(); devIt != devices->end(); devIt++)
    {
        auto dev = &devIt.value();

        // If device is not detected, ignore it.
        if (dev->detected)
        {
            if (!dev->disabled && !dev->connected)
            {
                        
                qInfo(logUsbControl()) << QString("Attempting to connect to %0").arg(dev->product);
                dev->handle = hid_open_path(dev->path.toLocal8Bit());

                if (dev->handle)
                {
                    qInfo(logUsbControl()) << QString("Connected to device: %0 from %1 S/N %2").arg(dev->product,dev->manufacturer,dev->serial);
                    hid_set_nonblocking(dev->handle, 1);
                    devicesConnected++;
                    dev->connected=true;

                    if (dev->type.model == RC28)
                    {
                        QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureLEDControl,1,"0"); });
                        QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureLEDControl,2,"0"); });
                        QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureLEDControl,3,"0"); });
                        QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureLEDControl,4,"1"); });
                    }
                    else if (dev->type.model == QuickKeys)
                    {
                        QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureEventsA); });
                        QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureEventsB); });
                    }

                    QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureSerial); });
                    QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureFirmware); });
                    QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureOverlay,5,"Hello from wfview"); });
                }
                else
                {
                    // This should only get displayed once if we fail to connect to a device
                    qInfo(logUsbControl()) << QString("Error connecting to  %0: %1")
                                              .arg(dev->product,QString::fromWCharArray(hid_error(dev->handle)));
                }
            }

            if (!dev->uiCreated) // Create ui for all detected devices (even disabled)
            {
                for (int i=0;i<dev->type.knobs;i++)
                {
                    dev->knobValues.append(KNOBVALUE());
                }


                // Find our defaults/knobs/buttons for this controller:
                // First see if we have any stored and add them to the list if not.

                if (dev->type.buttons > 0)
                {
                    auto bti = std::find_if(buttonList->begin(), buttonList->end(), [dev](const BUTTON& b)
                    { return (b.path == dev->path); });
                    if (bti == buttonList->end())
                    {
                        // List doesn't contain any buttons for this device so add default buttons to the end of buttonList
                        qInfo(logUsbControl()) << "No stored buttons found, loading defaults";
                        for (auto but=defaultButtons.begin();but!=defaultButtons.end();but++)
                        {
                            if (but->dev == dev->type.model)
                            {
                                but->path = dev->path;
                                buttonList->append(BUTTON(*but));
                            }
                        }
                    } else {
                        qInfo(logUsbControl()) << "Found stored buttons for this device, loading.";
                    }

                    // We need to set the parent device for all buttons belonging to this device!
                    for (auto but = buttonList->begin(); but != buttonList->end(); but++)
                    {
                        if (but->path == dev->path)
                        {
                            auto bon = std::find_if(commands.begin(), commands.end(), [but](const COMMAND& c) { return (c.text == but->on); });
                            if (bon != commands.end())
                                but->onCommand = &(*bon);
                            else
                                qWarning(logUsbControl()) << "On Command" << but->on << "not found";

                            auto boff = std::find_if(commands.begin(), commands.end(), [but](const COMMAND& c) { return (c.text == but->off); });
                            if (boff != commands.end())
                                but->offCommand = &(*boff);
                            else
                                qWarning(logUsbControl()) << "Off Command" << but->off << "not found";

                            but->parent = dev;
                        }
                    }
                }

                if (dev->type.knobs > 0)
                {
                    auto kbi = std::find_if(knobList->begin(), knobList->end(), [dev](const KNOB& k)
                    { return (k.path == dev->path); });
                    if (kbi == knobList->end())
                    {
                        qInfo(logUsbControl()) << "No stored knobs found, loading defaults";
                        for (auto kb = defaultKnobs.begin();kb != defaultKnobs.end(); kb++)
                        {
                            if (kb->dev == dev->type.model) {
                                kb->path = dev->path;
                                knobList->append(KNOB(*kb));
                            }
                        }
                    } else {
                        qInfo(logUsbControl()) << "Found stored knobs for this device, loading.";
                    }

                    for (auto kb = knobList->begin(); kb != knobList->end(); kb++)
                    {
                        if (kb->path == dev->path)
                        {
                            auto k = std::find_if(commands.begin(), commands.end(), [kb](const COMMAND& c) { return (c.text == kb->cmd); });
                            if (k != commands.end())
                                kb->command = &(*k);
                            else
                                qWarning(logUsbControl()) << "Knob Command" << kb->cmd << "not found";

                            kb->parent = dev;
                            if (kb->page == 1)
                                dev->knobValues[kb->num].name = kb->cmd;
                        }
                    }
                }
                // Let the UI know we have a new controller
                emit newDevice(dev);

            } else {
                emit setConnected(dev);
            }
        }
    }
    
    if (devicesConnected>0 && dataTimer == Q_NULLPTR) {
        dataTimer = new QTimer(this);
        connect(dataTimer, &QTimer::timeout, this, &usbController::runTimer);
        dataTimer->start(25);
    }
    
#ifndef USB_HOTPLUG
    // Run the periodic timer to check for new devices    
    QTimer::singleShot(2000, this, SLOT(run()));
#endif
}


/*
 * runTimer is called every 25ms once a connection to a supported controller is established
 * It will then step through each connected controller and request any data
*/

void usbController::runTimer()
{
    QMutexLocker locker(mutex);

    for (auto devIt = devices->begin(); devIt != devices->end(); devIt++)
    {
        auto dev = &devIt.value();

        if (dev->disabled || !dev->detected || !dev->connected || !dev->handle) {
            // This device isn't currently connected.
            continue;
        }

        int res=1;
        
        while (res > 0) {
            quint32 tempButtons = 0;
            QByteArray data(HIDDATALENGTH, 0x0);
            res = hid_read(dev->handle, (unsigned char*)data.data(), HIDDATALENGTH);
            if (res < 0)
            {
                qInfo(logUsbControl()) << "USB Device disconnected" << dev->product;
                hid_close(dev->handle);
                dev->handle = NULL;
                dev->detected = false;
                dev->connected = false;
                dev->detected = false;
                dev->remove = true;
                dev->uiCreated = false;
                devicesConnected--;
                if (devicesConnected == 0) {
                    dataTimer->stop();
                    delete dataTimer;
                    dataTimer = Q_NULLPTR;
                }
                emit removeDevice(dev);
                QTimer::singleShot(250, this, SLOT(run())); // Cleanup
                break;
            }

            if (res == 5 && (dev->type.model == shuttleXpress || dev->type.model == shuttlePro2))
            {
                tempButtons = ((quint8)data[4] << 8) | ((quint8)data[3] & 0xff);
                unsigned char tempJogpos = (unsigned char)data[1];
                unsigned char tempShutpos = (unsigned char)data[0];

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

                if (tempJogpos == dev->jogpos + 1 || (tempJogpos == 0 && dev->jogpos == 0xff))
                {
                    dev->knobValues[0].value++;
                }
                else if (tempJogpos != dev->jogpos) {
                    dev->knobValues[0].value--;
                }

                dev->jogpos = tempJogpos;
                dev->shutpos = tempShutpos;
            }
            else if ((res > 31) && dev->type.model == RC28)
            {
                // This is a response from the Icom RC28
                if ((unsigned char)data[0] == 0x02) {
                    qInfo(logUsbControl()) << QString("Received RC-28 Firmware Version: %0").arg(QString(data.mid(1,data.indexOf(" ")-1)));
                }
                else
                {
                    tempButtons |= !((quint8)data[5] ^ 0x06) << 0;
                    tempButtons |= !((quint8)data[5] ^ 0x05) << 1;
                    tempButtons |= !((quint8)data[5] ^ 0x03) << 2;
                    if ((unsigned char)data[5] == 0x07)
                    {
                        if ((unsigned char)data[3] == 0x01)
                        {
                            dev->knobValues[0].value = dev->knobValues[0].value + data[1];
                        }
                        else if ((unsigned char)data[3] == 0x02)
                        {
                            dev->knobValues[0].value = dev->knobValues[0].value - data[1];
                        }
                    }
                }
            }
            else if (res > 15 && dev->type.model == eCoderPlus && (quint8)data[0] == 0xff) {
                tempButtons = ((quint8)data[3] << 16) | ((quint8)data[2] << 8) | ((quint8)data[1] & 0xff);
                quint32 tempKnobs = ((quint8)data[16] << 24) | ((quint8)data[15] << 16) | ((quint8)data[14] << 8) | ((quint8)data[13]  & 0xff);
                
                for (unsigned char i = 0; i < dev->knobValues.size(); i++)
                {
                    if (dev->knobs != tempKnobs) {
                        // One of the knobs has moved
                        for (unsigned char i = 0; i < 4; i++) {
                            if ((tempKnobs >> (i * 8) & 0xff) != (dev->knobs >> (i * 8) & 0xff)) {
                                dev->knobValues[i].value = dev->knobValues[i].value + (qint8)((dev->knobs >> (i * 8)) & 0xff);
                            }
                        }
                        dev->knobs = tempKnobs;
                    }
                }
            }
            else if (res > 5 && dev->type.model == QuickKeys && (quint8)data[0] == 0x02) {

                if ((quint8)data[1] == 0xf0) {
                    
                    //qInfo(logUsbControl()) << "Received:" << data;
                    tempButtons = (data[3] << 8) | (data[2] & 0xff);

                    if (data[7] & 0x01) {
                        dev->knobValues[0].value++;
                    }
                    else if (data[7] & 0x02) {
                        dev->knobValues[0].value--;
                    }
                    
                }
                else if ((quint8)data[1] == 0xf2 && (quint8)data[2] == 0x01)
                {
                    // Battery level
                    quint8 battery = (quint8)data[3];
                    qDebug(logUsbControl()) << QString("Battery level %1 %").arg(battery);
                }
            }
            // Is it any model of StreamDeck?
            else if (res>=dev->type.buttons && dev->type.model != usbNone)
            {
                // Main buttons
                if (dev->type.model == usbDeviceType::StreamDeckOriginal)
                {

                    for (int i = dev->type.buttons-1;i>=0;i--) {
                        quint8 val = ((i - (i % dev->type.cols)) + (dev->type.cols-1)) - (i % dev->type.cols);
                        tempButtons |= ((quint8)data[val+1] & 0x01) << (i);
                    }

                    qInfo(logUsbControl()) << "RX:" << data.toHex(' ');
                }
                else
                {
                    if ((quint8)data[1] == 0x00)
                    {
                        for (int i = dev->type.buttons - dev->type.knobs;i>0;i--) {
                            tempButtons |= ((quint8)data[i+3] & 0x01) << (i-1);
                        }
                    }

                    // Knobs and secondary buttons
                    if (dev->type.model == StreamDeckPlus) {
                        if ((quint8)data[1] == 0x03 && (quint8)data[2] == 0x05)
                        {
                            // Knob action!
                            switch ((quint8)data[4])
                            {
                            case 0x00:
                                // Knob button
                                for (int i=dev->type.buttons;i>7;i--)
                                {
                                    tempButtons |= ((quint8)data[i-4] & 0x01) << (i-1);
                                }
                                break;
                            case 0x01:
                                // Knob moved
                                for (int i=0;i<dev->type.knobs;i++)
                                {
                                    dev->knobValues[i].value += (qint8)data[i+5];
                                }
                                break;
                            }
                        }
                        else if ((quint8)data[1] == 0x02 && (quint8)data[2] == 0x0E)
                        {
                            // LCD touch event
                            int x = ((quint8)data[7] << 8) | ((quint8)data[6] & 0xff);
                            int y = ((quint8)data[9] << 8) | ((quint8)data[8] & 0xff);
                            int x2=0;
                            int y2=0;
                            QString tt="";
                            switch ((quint8)data[4])
                            {
                            case 0x01:
                                tt="Short";
                                break;
                            case 0x02:
                                tt="Long";
                                break;
                            case 0x03:
                                tt="Swipe";
                                x2 = ((quint8)data[11] << 8) | ((quint8)data[10] & 0xff);
                                y2 = ((quint8)data[13] << 8) | ((quint8)data[12] & 0xff);
                                break;
                            }
                            qInfo(logUsbControl()) << QString("%0 touch: %1,%2 to %3,%4").arg(tt).arg(x).arg(y).arg(x2).arg(y2);
                        }
                    }
                }
            }

            // Step through all buttons and emit ones that have been pressed.
            // Only do it if actual data has been received.
            if (res > 0 && dev->buttons != tempButtons)
            {
                qDebug(logUsbControl()) << "Got Buttons:" << QString::number(tempButtons,2);
                // Step through all buttons and emit ones that have been pressed.
                for (unsigned char i = 0; i <dev->type.buttons; i++)
                {
                    auto but = std::find_if(buttonList->begin(), buttonList->end(), [dev, i](const BUTTON& b)
                    { return (b.path == dev->path && b.page == dev->currentPage && b.num == i); });
                    if (but != buttonList->end()) {
                        if ((!but->isOn) && ((tempButtons >> i & 1) && !(dev->buttons >> i & 1)))
                        {
                            qDebug(logUsbControl()) << QString("On Button event for button %0: %1").arg(but->num).arg(but->onCommand->text);
                            if (but->onCommand->command == cmdPageUp)
                                emit changePage(dev, dev->currentPage+1);
                            else if (but->onCommand->command == cmdPageDown)
                                emit changePage(dev, dev->currentPage-1);
                            else if (but->onCommand->command == cmdLCDSpectrum)
                                dev->lcd = cmdLCDSpectrum;
                            else if (but->onCommand->command == cmdLCDWaterfall)
                                dev->lcd = cmdLCDWaterfall;
                            else if (but->onCommand->command == cmdLCDNothing) {
                                dev->lcd = cmdLCDNothing;
                                QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureColor,i,"",Q_NULLPTR, &dev->color); });
                            }else {
                                emit button(but->onCommand);
                            }
                            // Change the button text to reflect the off Button
                            if (but->offCommand->index != 0) {
                                QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureButton,i,but->offCommand->text, but->icon, &but->backgroundOff); });
                            }
                            but->isOn=true;
                        }
                        else if ((but->toggle && but->isOn) && ((tempButtons >> i & 1) && !(dev->buttons >> i & 1)))
                        {
                            qDebug(logUsbControl()) << QString("Off Button (toggle) event for button %0: %1").arg(but->num).arg(but->onCommand->text);
                            if (but->offCommand->command == cmdPageUp)
                                emit changePage(dev, dev->currentPage+1);
                            else if (but->offCommand->command == cmdPageDown)
                                emit changePage(dev, dev->currentPage-1);
                            else if (but->offCommand->command == cmdLCDSpectrum)
                                dev->lcd = cmdLCDSpectrum;
                            else if (but->offCommand->command == cmdLCDWaterfall)
                                dev->lcd = cmdLCDWaterfall;
                            else if (but->offCommand->command == cmdLCDNothing) {
                                dev->lcd = cmdLCDNothing;
                                QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureColor,i,"",Q_NULLPTR, &dev->color); });
                            } else {
                                emit button(but->offCommand);
                            }
                            QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureButton,i,but->onCommand->text, but->icon, &but->backgroundOn); });
                            but->isOn=false;
                        }
                        else if ((!but->toggle && but->isOn) && ((dev->buttons >> i & 1) && !(tempButtons >> i & 1)))
                        {
                            if (but->offCommand->command == cmdLCDSpectrum)
                                dev->lcd = cmdLCDSpectrum;
                            else if (but->offCommand->command == cmdLCDWaterfall)
                                dev->lcd = cmdLCDWaterfall;
                            else if (but->offCommand->command == cmdLCDNothing) {
                                QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureColor,i,"",Q_NULLPTR, &dev->color); });
                                dev->lcd = cmdLCDNothing;
                            } else
                            {
                                qDebug(logUsbControl()) << QString("Off Button event for button %0: %1").arg(but->num).arg(but->offCommand->text);
                                emit button(but->offCommand);
                            }
                            // Change the button text to reflect the on Button
                            QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureButton,i,but->onCommand->text, but->icon, &but->backgroundOn); });
                            but->isOn=false;
                        }
                    }
                }
                dev->buttons = tempButtons;
            }

            // As the knobs can move very fast, only send every 100ms so the rig isn't overwhelmed
            if (dev->lastusbController.msecsTo(QTime::currentTime()) >= 100 || dev->lastusbController > QTime::currentTime())
            {
                if (dev->type.model == shuttleXpress || dev->type.model == shuttlePro2)
                {
                    if (dev->shutpos > 0  && dev->shutpos < 0x08)
                    {
                        dev->shutMult = dev->shutpos;
                        emit doShuttle(true, dev->shutMult);
                        qDebug(logUsbControl()) << "Shuttle PLUS" << dev->shutMult;
                        
                    }
                    else if (dev->shutpos > 0xEF) {
                        dev->shutMult = abs(dev->shutpos - 0xff) + 1;
                        emit doShuttle(false, dev->shutMult);
                        qDebug(logUsbControl()) << "Shuttle MINUS" << dev->shutMult;
                    }
                }
                
                for (unsigned char i = 0; i < dev->knobValues.size(); i++)
                {
                    auto kb = std::find_if(knobList->begin(), knobList->end(), [dev, i](const KNOB& k)
                    { return (k.command && k.path == dev->path && k.page == dev->currentPage && k.num == i && dev->knobValues[i].value != dev->knobValues[i].previous); });

                    if (kb != knobList->end()) {
                        // sendCommand mustn't be deleted so we ensure it stays in-scope by declaring it private (we will only ever send one command).
                        sendCommand = *kb->command;
                        if (sendCommand.command != cmdSetFreq) {
                            int tempVal = dev->knobValues[i].value * dev->sensitivity;
                            tempVal = qMin(qMax(tempVal,0),255);
                            sendCommand.suffix = quint8(tempVal);
                            dev->knobValues[i].value=tempVal/dev->sensitivity; // This ensures that dial can't go outside 0-255
                            dev->knobValues[i].name = kb->command->text;
                            QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureGraph,i,"",Q_NULLPTR,&dev->color); });
                        }
                        else
                        {
                            sendCommand.value = dev->knobValues[i].value/dev->sensitivity;
                        }
                        
                        emit button(&sendCommand);

                        if (sendCommand.command == cmdSetFreq) {
                            dev->knobValues[i].value = 0;
                        }
                        dev->knobValues[i].previous=dev->knobValues[i].value;
                    }
                }
                
                dev->lastusbController = QTime::currentTime();
            }

        }
    }
}

void usbController::receivePTTStatus(bool on) {
    static QColor lastColour = currentColour;
    static bool ptt;

    for (auto devIt = devices->begin(); devIt != devices->end(); devIt++)
    {
        auto dev = &devIt.value();
        if (dev->lcd != cmdLCDSpectrum && dev->lcd != cmdLCDWaterfall) {
            if (on && !ptt) {
                lastColour = currentColour;
                QColor newColor = QColor(255,0,0);
                sendRequest(dev,usbFeatureType::featureLEDControl,1,"1");
                sendRequest(dev,usbFeatureType::featureColor,0, "", Q_NULLPTR, &newColor);
            }
            else {
                sendRequest(dev,usbFeatureType::featureLEDControl,1,"0");
                sendRequest(dev,usbFeatureType::featureColor,0, "", Q_NULLPTR, &lastColour);
            }
        }
    }

    ptt = on;
}

void usbController::sendToLCD(QImage* img)
{

    for (auto devIt = devices->begin(); devIt != devices->end(); devIt++)
    {
        auto dev = &devIt.value();
        sendRequest(dev,usbFeatureType::featureLCD,0,"",img);
    }
}


// We rely on being able to fallthrough case
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

/* This function will handle various commands for multiple models of controller
 *
 * Possible features are:
 * featureInit, featureFirmware, featureSerial, featureButton, featureSensitivity, featureBrightness,
 * featureOrientation, featureSpeed, featureColor, featureOverlay, featureTimeout, featurePages, featureDisable
*/
void usbController::sendRequest(USBDEVICE *dev, usbFeatureType feature, int val, QString text, QImage* img, QColor* color)
{
    if (dev == Q_NULLPTR || !dev->connected || dev->disabled || !dev->handle)
        return;

    QMutexLocker locker(mutex);

    QByteArray data(64, 0x0);
    QByteArray data2;
    int res=0;
    bool sdv1=false;

    // If feature is sensitivity, this is not model dependant and will update the internal sensitivity divider.
    if (feature == usbFeatureType::featureSensitivity)
    {
        dev->sensitivity=val;
        return;
    }

    switch (dev->type.model)
    {
    case QuickKeys:
        data.resize(32);
        data[0] = (qint8)0x02;

        switch (feature) {
        case usbFeatureType::featureEventsA:
            data[1] = (qint8)0xb0;
            data[2] = (qint8)0x04;
            break;
        case usbFeatureType::featureEventsB:
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x10;
            break;
        case usbFeatureType::featureButton:
            if (val > 7) {
                return;
            }
            text = text.mid(0, 10); // Make sure text is no more than 10 characters.
            qDebug(logUsbControl()) << QString("Programming button %0 with %1").arg(val).arg(text);
            data[1] = (qint8)0xb1;
            data[3] = val + 1;
            data[5] = text.length() * 2;
            data2 = qToLittleEndian(QByteArray::fromRawData(reinterpret_cast<const char*>(text.constData()), text.size() * 2));
            data.replace(16, data2.size(), data2);
            break;
        case usbFeatureType::featureBrightness:
            qDebug(logUsbControl()) << QString("Setting brightness to %0").arg(val);
            data[1] = (qint8)0xb1;
            data[2] = (qint8)0x0a;
            data[3] = (qint8)0x01;
            data[4] = val;
            dev->brightness = val;
            break;
        case usbFeatureType::featureOrientation:
            data[1] = (qint8)0xb1;
            data[2] = (qint8)val+1;
            dev->orientation = val;
            break;
        case usbFeatureType::featureSpeed:
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x04;
            data[3] = (qint8)0x01;
            data[4] = (qint8)0x01;
            data[5] = val+1;
            dev->speed = val;
            break;
        case usbFeatureType::featureColor: {
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x01;
            data[3] = (qint8)0x01;
            data[6] = (qint8)color->red();
            data[7] = (qint8)color->green();
            data[8] = (qint8)color->blue();
            if (val) {
               currentColour = *color;
               dev->color=currentColour;
            }
            break;
        }
        case usbFeatureType::featureOverlay:
            data[1] = (qint8)0xb1;
            data[3] = (qint8)val;
            for (int i = 0; i < text.length(); i = i + 8)
            {
                data[2] = (i == 0) ? 0x05 : 0x06;
                data2 = qToLittleEndian(QByteArray::fromRawData(reinterpret_cast<const char*>(text.mid(i, 8).constData()), text.mid(i, 8).size() * 2));
                data.replace(16, data2.size(), data2);
                data[5] = text.mid(i, 8).length() * 2;
                data[6] = (i > 0 && text.mid(i).size() > 8) ? 0x01 : 0x00;
                hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
            }
            break;
        case usbFeatureType::featureTimeout:
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x08;
            data[3] = (qint8)0x01;
            data[4] = val;
            dev->timeout = val;
            break;
        default:
            // Command not supported by this device so return.
            return;
            break;
        }
        data.replace(10, dev->deviceId.size(), dev->deviceId.toLocal8Bit());
        hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
        break;

        // Below are Stream Deck Generation 1 h/w
    case StreamDeckOriginal:
    case StreamDeckMini:
    case StreamDeckMiniV2:
        data.resize(17);
        sdv1=true;
        // Below are StreamDeck Generation 2 h/w
    case StreamDeckOriginalMK2:
    case StreamDeckOriginalV2:
    case StreamDeckXL:
    case StreamDeckXLV2:
    case StreamDeckPlus:
    case StreamDeckPedal:
        if (!sdv1)
            data.resize(32);

        switch (feature)
        {
        case usbFeatureType::featureFirmware:
            if (sdv1) {
                data[0] = 0x04;
            } else {
                data[0] = 0x05;
            }
            hid_get_feature_report(dev->handle,(unsigned char*)data.data(),(size_t)data.size());
            qInfo(logUsbControl()) << QString("%0: Firmware = %1").arg(dev->product,QString::fromLatin1(data.mid(2,12)));
            break;
        case usbFeatureType::featureSerial:
            if (sdv1) {
                data[0] = 0x03;
            } else {
                data[0] = 0x06;
            }
            hid_get_feature_report(dev->handle,(unsigned char*)data.data(),(size_t)data.size());
            qInfo(logUsbControl()) << QString("%0: Serial Number = %1").arg(dev->product,QString::fromLatin1(data.mid(5,8)));
            break;
        case usbFeatureType::featureReset:
            if (sdv1) {
                data[0] = (qint8)0x0b;
                data[1] = (qint8)0x63;
            } else {
                data[0] = (qint8)0x03;
                data[1] = (qint8)0x02;
            }
            hid_send_feature_report(dev->handle, (const unsigned char*)data.constData(), data.size());
            break;
        case usbFeatureType::featureResetKeys:
            data.resize(dev->type.maxPayload);
            memset(data.data(),0x0,data.size());
            data[0] = (qint8)0x02;
            res=hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
            break;
        case usbFeatureType::featureBrightness:
            if (sdv1) {
                data[0] = (qint8)0x05;
                data[1] = (qint8)0x55;
                data[2] = (qint8)0xaa;
                data[3] = (qint8)0xd1;
                data[4] = (qint8)0x01;
                data[5] = val*25;
            } else {
                data[0] = (qint8)0x03;
                data[1] = (qint8)0x08;
                data[2] = val*33; // Stream Deck brightness is in %
            }
            res = hid_send_feature_report(dev->handle, (const unsigned char*)data.constData(), data.size());
            dev->brightness = val;
            break;
        case usbFeatureType::featureSensitivity:
            dev->sensitivity=val;
            break;
        case usbFeatureType::featureGraph:
        {
            if (dev->type.model == usbDeviceType::StreamDeckPlus && val < dev->type.knobs)
            {
                QImage image(200,25, QImage::Format_RGB888);
                if (text != "**REMOVE**") { // && dev->knobValues[val].lastChanged + 1900 < QDateTime::currentMSecsSinceEpoch()) {
                    image.fill(Qt::black);
                    QPainter paint(&image);
                    int x=qMin(190,(dev->knobValues[val].value * dev->sensitivity) * 190 / 255);
                    paint.fillRect(5,5,x,20, Qt::darkGreen);
                    paint.setFont(QFont("times",10));
                    paint.setPen(Qt::white);
                    int perc=qMin(100,(dev->knobValues[val].value * dev->sensitivity) * 100 / 255);
                    paint.drawText(5,5,190,20, Qt::AlignCenter | Qt::AlignVCenter, QString("%0 %1%").arg(dev->knobValues[val].name).arg(perc));
                }
                else if (dev->knobValues[val].lastChanged + 1900 < QDateTime::currentMSecsSinceEpoch())
                {
                    image.fill(*color);
                } else {
                    // Value has changed.
                    break;
                }
                data2.clear();
                QBuffer buffer(&data2);
                image.save(&buffer, "JPG");

                quint32 rem = data2.size();
                quint16 index = 0;

                streamdeck_lcd_header h;
                memset(h.packet, 0x0, sizeof(h)); // We can't be sure it is initialized with 0x00!
                h.cmd = 0x02;
                h.suffix = 0x0c;
                h.x=val*200;
                h.y=75;
                h.width=200;
                h.height=25;

                while (rem > 0)
                {
                    quint16 length = qMin(quint16(rem),quint16(dev->type.maxPayload-sizeof(h)));
                    data.clear();
                    h.isLast = (quint8)(rem <= dev->type.maxPayload-sizeof(h) ? 1 : 0); // isLast ? 1 : 0,3
                    h.length = length;
                    h.index = index;
                    rem -= length;
                    data.append(QByteArray::fromRawData((const char*)h.packet,sizeof(h)));
                    data.append(data2.mid(0,length));
                    data.resize(dev->type.maxPayload);
                    memset(data.data()+length+sizeof(h),0x0,data.size()-(length+sizeof(h)));
                    res=hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
                    //qInfo(logUsbControl()) << "Sending" << (((quint8)data[7] << 8) | ((quint8)data[6] & 0xff)) << "total=" << data.size()  << "payload=" << (((quint8)data[5] << 8) | ((quint8)data[4] & 0xff)) << "last" << (quint8)data[3];
                    data2.remove(0,length);
                    index++;
                }
                if (text != "**REMOVE**") {
                    dev->knobValues[val].lastChanged = QDateTime::currentMSecsSinceEpoch();
                    if (dev->lcd != cmdLCDSpectrum && dev->lcd != cmdLCDWaterfall)
                        QTimer::singleShot(2000, this, [=]() { sendRequest(dev,usbFeatureType::featureGraph,val,"**REMOVE**",Q_NULLPTR,&dev->color); });
                }

            }
            break;
        }
        case usbFeatureType::featureColor:
            dev->color = *color;
        case usbFeatureType::featureOverlay:
        {
            if (dev->type.model == usbDeviceType::StreamDeckPlus)
            {
                QImage image(800,100, QImage::Format_RGB888);
                QPainter paint(&image);
                if (val) {
                    paint.setFont(QFont("times",16));
                    paint.fillRect(image.rect(), dev->color);
                    paint.drawText(image.rect(),Qt::AlignCenter | Qt::AlignVCenter, text);
                    if (val)
                       QTimer::singleShot(val*1000, this, [=]() { sendRequest(dev,usbFeatureType::featureOverlay); });
                } else {
                    paint.fillRect(image.rect(), dev->color);
                }
                QBuffer buffer(&data2);
                image.save(&buffer, "JPG");
            }
        }
        case usbFeatureType::featureLCD:
        {
            if (dev->type.model == usbDeviceType::StreamDeckPlus)
            {
                if (img != Q_NULLPTR)
                {
                    QImage image = img->scaled(800,100,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                    QPainter paint(&image);

                    for (int i=0;i<dev->type.knobs;i++) {
                        if (QDateTime::currentMSecsSinceEpoch() < dev->knobValues[i].lastChanged + 2000)
                        {
                            paint.fillRect(200*i,75,200,25, Qt::black);
                            int x=qMin(190,(dev->knobValues[i].value * dev->sensitivity) * 190 / 255);
                            paint.fillRect((200*i)+5,80,x,20, Qt::darkGreen);
                            paint.setFont(QFont("times",10));
                            paint.setPen(Qt::white);
                            int perc=qMin(100,(dev->knobValues[i].value * dev->sensitivity) * 100 / 255);
                            paint.drawText((200*i)+5,80,190,20, Qt::AlignCenter | Qt::AlignVCenter, QString("%0 %1%").arg(dev->knobValues[i].name).arg(perc));
                        }
                    }
                    data2.clear();
                    QBuffer buffer(&data2);
                    image.save(&buffer, "JPG");
                }
                quint32 rem = data2.size();
                quint16 index = 0;

                streamdeck_lcd_header h;
                memset(h.packet, 0x0, sizeof(h)); // We can't be sure it is initialized with 0x00!
                h.cmd = 0x02;
                h.suffix = 0x0c;
                h.x=0;
                h.y=0;
                h.width=800;
                h.height=100;

                while (rem > 0)
                {
                    quint16 length = qMin(quint16(rem),quint16(dev->type.maxPayload-sizeof(h)));
                    data.clear();
                    h.isLast = (quint8)(rem <= dev->type.maxPayload-sizeof(h) ? 1 : 0); // isLast ? 1 : 0,3
                    h.length = length;
                    h.index = index;
                    rem -= length;
                    data.append(QByteArray::fromRawData((const char*)h.packet,sizeof(h)));
                    data.append(data2.mid(0,length));
                    data.resize(dev->type.maxPayload);
                    memset(data.data()+length+sizeof(h),0x0,data.size()-(length+sizeof(h)));
                    res=hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
                    //qInfo(logUsbControl()) << "Sending" << (((quint8)data[7] << 8) | ((quint8)data[6] & 0xff)) << "total=" << data.size()  << "payload=" << (((quint8)data[5] << 8) | ((quint8)data[4] & 0xff)) << "last" << (quint8)data[3];
                    data2.remove(0,length);
                    index++;
                }
            }
            break;
        }
        case usbFeatureType::featureButton: {
            // StreamDeckPedal is the only model without oled buttons
            // Plus has 12 buttons but only 8 oled
            if (dev->type.model != usbDeviceType::StreamDeckPedal &&
                ((dev->type.model == usbDeviceType::StreamDeckPlus  && val < 8) ||
                (val < dev->type.buttons)))
            {
                if (val < 8 || dev->type.model != usbDeviceType::StreamDeckPlus) {

                    QImage butImage(dev->type.iconSize,dev->type.iconSize, QImage::Format_RGB888);
                    if (color != Q_NULLPTR)
                        butImage.fill(*color);
                    else
                        butImage.fill(dev->color);

                    QPainter butPaint(&butImage);

                    if ( img == Q_NULLPTR) {
                        if (dev->type.iconSize == 72)
                            butPaint.setFont(QFont("times",10));
                        else
                            butPaint.setFont(QFont("times",16));

                        butPaint.drawText(butImage.rect(),Qt::AlignCenter | Qt::AlignVCenter, text);
                    } else {
                        butPaint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                        butPaint.drawImage(0, 0, *img);
                    }

                    QBuffer butBuffer(&data2);
                    QTransform myTransform;
                    if (dev->type.model == usbDeviceType::StreamDeckOriginal || dev->type.model == usbDeviceType::StreamDeckXL)
                    {
                        myTransform.rotate(180);
                    }
                    QImage myImage = butImage.transformed(myTransform);

                    if (sdv1)
                    {

                        myImage.save(&butBuffer, "BMP");
                        quint16 payloadLen = dev->type.maxPayload - sizeof(streamdeck_v1_image_header);

                        if (dev->type.model == usbDeviceType::StreamDeckOriginal) {
                            // Special case for buttons on original StreamDeck
                            val = ((val - (val % dev->type.cols)) + (dev->type.cols-1)) - (val % dev->type.cols) + 1;
                            payloadLen = data2.size()/2;
                        }

                        quint32 rem = data2.size();
                        quint16 index = 1;
                        streamdeck_v1_image_header h1;
                        memset(h1.packet, 0x0, sizeof(h1)); // We can't be sure it is initialized with 0x00!
                        h1.cmd = 0x02;
                        h1.suffix = 0x01;
                        h1.button = val;
                        while (rem > 0)
                        {
                            quint32 length = qMin(quint16(rem),quint16(payloadLen));
                            data.clear();
                            data.squeeze();
                            h1.isLast = (quint8)(rem <= payloadLen ? 1 : 0); // isLast ? 1 : 0,3
                            h1.index = index;
                            data.append(QByteArray::fromRawData((const char*)h1.packet,0x16));
                            data.resize(dev->type.maxPayload);
                            rem -= length;
                            data=data.replace(0x10,length,data2.mid(0,length));
                            res=hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
                            //qInfo(logUsbControl()) << "Sending len=" << dev->type.maxPayload << h1.index << "total=" << data.size()  << "payload=" << length << "last" << h1.isLast;
                            data2.remove(0,length);
                            index++;
                        }
                    }
                    else
                    {
                        myImage.save(&butBuffer, "JPG");
                        quint32 rem = data2.size();
                        quint16 index = 0;
                        streamdeck_image_header h;
                        memset(h.packet, 0x0, sizeof(h)); // We can't be sure it is initialized with 0x00!
                        h.cmd = 0x02;
                        h.suffix = 0x07;
                        h.button = val;
                        while (rem > 0)
                        {
                            quint16 length = qMin(quint16(rem),quint16(dev->type.maxPayload-sizeof(h)));
                            data.clear();
                            h.isLast = (quint8)(rem <= dev->type.maxPayload-sizeof(h) ? 1 : 0); // isLast ? 1 : 0,3
                            h.length = length;
                            h.index = index;
                            data.append(QByteArray::fromRawData((const char*)h.packet,sizeof(h)));
                            rem -= length;
                            data.append(data2.mid(0,length));
                            data.resize(dev->type.maxPayload);
                            memset(data.data()+length+sizeof(h),0x0,data.size()-(length+sizeof(h)));
                            res=hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
                             //qInfo(logUsbControl()) << "Sending" << (((quint8)data[7] << 8) | ((quint8)data[6] & 0xff)) << "total=" << data.size()  << "payload=" << (((quint8)data[5] << 8) | ((quint8)data[4] & 0xff)) << "last" << (quint8)data[3];
                            data2.remove(0,length);
                            index++;
                        }
                    }
                }
            }
        }
        default:
            break;
        }
        break;
    case RC28:
        data.resize(3);
        memset(data.data(),0x0,data.size());
        switch (feature)
        {
        case usbFeatureType::featureFirmware:
            data[0] = 63;
            data[1] = 0x02;
            break;
        case usbFeatureType::featureLEDControl:
            data[1] = 0x01;
            if (text == "0")
                dev->ledStatus |= 1UL << (val-1);
            else
                dev->ledStatus &= ~(1UL << (val-1));
            data[2] = dev->ledStatus;
            break;
        default:
            return; // No command
            break;
        }
        res = hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
        break;
    default:
        break;
    }

    if (res == -1)
        qInfo(logUsbControl()) << "Command" << feature << "returned" << res;
}

// Don't allow fallthrough elsewhere in the file.
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

void usbController::programDisable(USBDEVICE* dev, bool disabled)
{
    dev->disabled = disabled;

    if (disabled)
    {
        // Disconnect the device:
        if (dev->handle) {
            qInfo(logUsbControl()) << "Disconnecting device:" << dev->product;
            sendRequest(dev,usbFeatureType::featureOverlay,60,"Goodbye from wfview");

            if (dev->type.model == RC28) {
                sendRequest(dev,usbFeatureType::featureLEDControl,4,"1");
            }

            QMutexLocker locker(mutex);

            hid_close(dev->handle);
            dev->connected=false;
            dev->handle=NULL;
        }
    } else {
        qInfo(logUsbControl()) << "Enabling device:" << dev->product;
    }
    QTimer::singleShot(250, this, SLOT(run())); // Call run disconnect/reconnect after 250ms
}


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
    defaultButtons.append(BUTTON(RC28, 0, QRect(52, 445, 238, 64), Qt::red, &commands[1], &commands[2],false,1)); // PTT On/OFF
    defaultButtons.append(BUTTON(RC28, 1, QRect(52, 373, 98, 46), Qt::red, &commands[0], &commands[0],false,2));
    defaultButtons.append(BUTTON(RC28, 2, QRect(193, 373, 98, 46), Qt::red, &commands[0], &commands[0],false,3));
    
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

    // StreamDeckOriginal
    defaultButtons.append(BUTTON(StreamDeckOriginal, 0, QRect(65, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 1, QRect(165, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 2, QRect(263, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 3, QRect(364, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 4, QRect(462, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 5, QRect(65, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 6, QRect(165, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 7, QRect(263, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 8, QRect(364, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 9, QRect(462, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 10, QRect(65, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 11, QRect(165, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 12, QRect(263, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 13, QRect(364, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginal, 14, QRect(462, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));

    // StreamDeckOriginalMK2
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 0, QRect(65, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 1, QRect(165, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 2, QRect(263, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 3, QRect(364, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 4, QRect(462, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 5, QRect(65, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 6, QRect(165, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 7, QRect(263, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 8, QRect(364, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 9, QRect(462, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 10, QRect(65, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 11, QRect(165, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 12, QRect(263, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 13, QRect(364, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalMK2, 14, QRect(462, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));

    // StreamDeckOriginalV2
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 0, QRect(65, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 1, QRect(165, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 2, QRect(263, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 3, QRect(364, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 4, QRect(462, 91, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 5, QRect(65, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 6, QRect(165, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 7, QRect(263, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 8, QRect(364, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 9, QRect(462, 190, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 10, QRect(65, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 11, QRect(165, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 12, QRect(263, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 13, QRect(364, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckOriginalV2, 14, QRect(462, 291, 75, 75), Qt::white, &commands[0], &commands[0],true));

    // StreamDeckMini
    defaultButtons.append(BUTTON(StreamDeckMini, 0, QRect(113, 86, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMini, 1, QRect(252, 86, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMini, 2, QRect(388, 86, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMini, 3, QRect(113, 204, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMini, 4, QRect(252, 204, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMini, 5, QRect(388, 204, 100, 80), Qt::white, &commands[0], &commands[0],true));

    // StreamDeckMiniV2
    defaultButtons.append(BUTTON(StreamDeckMiniV2, 0, QRect(113, 86, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMiniV2, 1, QRect(252, 86, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMiniV2, 2, QRect(388, 86, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMiniV2, 3, QRect(113, 204, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMiniV2, 4, QRect(252, 204, 100, 80), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckMiniV2, 5, QRect(388, 204, 100, 80), Qt::white, &commands[0], &commands[0],true));

    // StreamDeckXL
    defaultButtons.append(BUTTON(StreamDeckXL, 0, QRect(80, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 1, QRect(168, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 2, QRect(258, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 3, QRect(349, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 4, QRect(438, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 5, QRect(529, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 6, QRect(618, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 7, QRect(711, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 8, QRect(80, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 9, QRect(168, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 10, QRect(258, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 11, QRect(349, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 12, QRect(438, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 13, QRect(529, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 14, QRect(618, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 15, QRect(711, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 16, QRect(80, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 17, QRect(168, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 18, QRect(258, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 19, QRect(349, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 20, QRect(438, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 21, QRect(529, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 22, QRect(618, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 23, QRect(711, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 24, QRect(80, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 25, QRect(168, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 26, QRect(258, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 27, QRect(349, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 28, QRect(438, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 29, QRect(529, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 30, QRect(618, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXL, 31, QRect(711, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));

    // StreamDeckXLV2
    defaultButtons.append(BUTTON(StreamDeckXLV2, 0, QRect(80, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 1, QRect(168, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 2, QRect(258, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 3, QRect(349, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 4, QRect(438, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 5, QRect(529, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 6, QRect(618, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 7, QRect(711, 106, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 8, QRect(80, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 9, QRect(168, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 10, QRect(258, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 11, QRect(349, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 12, QRect(438, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 13, QRect(529, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 14, QRect(618, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 15, QRect(711, 195, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 16, QRect(80, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 17, QRect(168, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 18, QRect(258, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 19, QRect(349, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 20, QRect(438, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 21, QRect(529, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 22, QRect(618, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 23, QRect(711, 287, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 24, QRect(80, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 25, QRect(168, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 26, QRect(258, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 27, QRect(349, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 28, QRect(438, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 29, QRect(529, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 30, QRect(618, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckXLV2, 31, QRect(711, 376, 61, 61), Qt::white, &commands[0], &commands[0],true));

    // StreamDeckPedal
    defaultButtons.append(BUTTON(StreamDeckPedal, 0, QRect(7, 47, 115, 347), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPedal, 1, QRect(155, 47, 291, 353), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPedal, 2, QRect(475, 47, 115, 347), Qt::white, &commands[0], &commands[0]));

    // StreamDeckPlus
    defaultButtons.append(BUTTON(StreamDeckPlus, 0, QRect(75, 45, 63, 63), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckPlus, 1, QRect(202, 45, 63, 63), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckPlus, 2, QRect(330, 45, 63, 63), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckPlus, 3, QRect(458, 45, 63, 63), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckPlus, 4, QRect(75, 128, 63, 63), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckPlus, 5, QRect(202, 128, 63, 63), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckPlus, 6, QRect(330, 128, 63, 63), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckPlus, 7, QRect(458, 128, 63, 63), Qt::white, &commands[0], &commands[0],true));
    defaultButtons.append(BUTTON(StreamDeckPlus, 8, QRect(74, 358, 64, 52), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 9, QRect(204, 358, 64, 52), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 10, QRect(332, 358, 64, 52), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 11, QRect(462, 358, 64, 52), Qt::white, &commands[0], &commands[0]));
}

void usbController::loadKnobs()
{
    defaultKnobs.clear();
    defaultKnobs.append(KNOB(shuttleXpress, 0, QRect(205, 189, 203, 203), Qt::green, &commands[3]));
    defaultKnobs.append(KNOB(shuttlePro2, 0, QRect(104, 164, 124, 119), Qt::green, &commands[3]));
    defaultKnobs.append(KNOB(RC28, 0, QRect(78, 128, 184, 168), Qt::green, &commands[3]));
    defaultKnobs.append(KNOB(QuickKeys, 0, QRect(114, 130, 121, 43), Qt::green, &commands[3]));
    
    // eCoder
    defaultKnobs.append(KNOB(eCoderPlus, 0, QRect(173, 360, 205, 209), Qt::green, &commands[3]));
    defaultKnobs.append(KNOB(eCoderPlus, 1, QRect(120, 153, 72, 27), Qt::green, &commands[0]));
    defaultKnobs.append(KNOB(eCoderPlus, 2, QRect(242, 153, 72, 27), Qt::green, &commands[0]));
    defaultKnobs.append(KNOB(eCoderPlus, 3, QRect(362, 153, 72, 27), Qt::green, &commands[0]));

    // StreamDeckPlus
    defaultKnobs.append(KNOB(StreamDeckPlus, 0, QRect(74, 413, 64, 28), Qt::green, &commands[3]));
    defaultKnobs.append(KNOB(StreamDeckPlus, 1, QRect(204, 413, 64, 28), Qt::green, &commands[0]));
    defaultKnobs.append(KNOB(StreamDeckPlus, 2, QRect(332, 413, 64, 28), Qt::green, &commands[0]));
    defaultKnobs.append(KNOB(StreamDeckPlus, 3, QRect(462, 413, 64, 28), Qt::green, &commands[0]));
}

void usbController::loadCommands()
{
    commands.clear();
    int num = 0;
    // Important commands at the top!
    commands.append(COMMAND(num++, "None", commandAny, cmdNone, (quint8)0x0));
    commands.append(COMMAND(num++, "PTT On", commandButton, cmdSetPTT, cmdGetPTT, (quint8)0x1));
    commands.append(COMMAND(num++, "PTT Off", commandButton, cmdSetPTT, cmdGetPTT, (quint8)0x0));
    commands.append(COMMAND(num++, "VFOA", commandKnob, cmdSetFreq, cmdGetFreq, (quint8)0x0));
    commands.append(COMMAND(num++, "VFOB", commandKnob, cmdSetFreq, cmdGetFreq, (quint8)0x1));
    commands.append(COMMAND(num++, "Freq Down", commandButton, cmdSetFreq, cmdGetFreq, (int)-1));
    commands.append(COMMAND(num++, "Freq Up", commandButton, cmdSetFreq, cmdGetFreq, (int)1));
    commands.append(COMMAND(num++, "PTT Off", commandButton, cmdSetPTT, cmdGetPTT, (quint8)0x0));
    commands.append(COMMAND(num++, "PTT Toggle", commandButton, cmdPTTToggle, cmdGetPTT, (quint8)0x0));
    commands.append(COMMAND(num++, "Span/Step", commandKnob, cmdSeparator, (quint8)0x0));
    commands.append(COMMAND(num++, "Tune", commandButton, cmdStartATU, (quint8)0x0));
    commands.append(COMMAND(num++, "Span/Step", commandButton, cmdSeparator, (quint8)0x0));
    commands.append(COMMAND(num++, "Step+", commandButton, cmdSetStepUp, (quint8)0x0));
    commands.append(COMMAND(num++, "Step-", commandButton, cmdSetStepDown, (quint8)0x0));
    commands.append(COMMAND(num++, "Span+", commandButton, cmdSetSpanUp, (quint8)0x0));
    commands.append(COMMAND(num++, "Span-", commandButton, cmdSetSpanDown, (quint8)0x0));
    commands.append(COMMAND(num++, "Modes", commandButton, cmdSeparator, (quint8)0x0));
    commands.append(COMMAND(num++, "Mode+", commandButton, cmdSetModeUp, (quint8)0x0));
    commands.append(COMMAND(num++, "Mode-", commandButton, cmdSetModeDown, (quint8)0x0));
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
    commands.append(COMMAND(num++, "Bands", commandButton, cmdSeparator, (quint8)0x0));
    commands.append(COMMAND(num++, "Band+", commandButton, cmdSetBandUp, (quint8)0x0));
    commands.append(COMMAND(num++, "Band-", commandButton, cmdSetBandDown, (quint8)0x0));
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
    commands.append(COMMAND(num++, "NB/NR", commandButton, cmdSeparator, (quint8)0x0));
    commands.append(COMMAND(num++, "NR On", commandButton, cmdSetNR, cmdGetNR, (quint8)0x01));
    commands.append(COMMAND(num++, "NR Off", commandButton, cmdSetNR, cmdGetNR, (quint8)0x0));
    commands.append(COMMAND(num++, "NB On", commandButton, cmdSetNB, cmdGetNB, (quint8)0x01));
    commands.append(COMMAND(num++, "NB Off", commandButton, cmdSetNB, cmdGetNB, (quint8)0x0));
    commands.append(COMMAND(num++, "Moni On", commandButton, cmdSetMonitor, cmdGetMonitor, (quint8)0x01));
    commands.append(COMMAND(num++, "Moni Off", commandButton, cmdSetMonitor, cmdGetMonitor, (quint8)0x0));
    commands.append(COMMAND(num++, "Comp On", commandButton, cmdSetComp, cmdGetComp, (quint8)0x01));
    commands.append(COMMAND(num++, "Comp Off", commandButton, cmdSetComp, cmdGetComp, (quint8)0x0));
    commands.append(COMMAND(num++, "Vox On", commandButton, cmdSetVox, cmdGetVox, (quint8)0x01));
    commands.append(COMMAND(num++, "Vox Off", commandButton, cmdSetVox, cmdGetVox, (quint8)0x0));
    commands.append(COMMAND(num++, "Split", commandButton, cmdNone, (quint8)0x0));
    commands.append(COMMAND(num++, "Split On", commandButton, cmdSetQuickSplit, cmdGetDuplexMode, (quint8)0x01));
    commands.append(COMMAND(num++, "Split Off", commandButton, cmdSetQuickSplit, cmdGetDuplexMode, (quint8)0x0));
    commands.append(COMMAND(num++, "Swap VFO", commandButton, cmdVFOSwap, (quint8)0x0));
    commands.append(COMMAND(num++, "Scope", commandButton, cmdNone, (quint8)0x0));
    commands.append(COMMAND(num++, "Spectrum", commandButton, cmdLCDSpectrum, (quint8)0x0));
    commands.append(COMMAND(num++, "Waterfall", commandButton, cmdLCDWaterfall, (quint8)0x0));
    commands.append(COMMAND(num++, "No Display", commandButton, cmdLCDNothing, (quint8)0x0));
    commands.append(COMMAND(num++, "Pages", commandButton, cmdSeparator, (quint8)0x0));
    commands.append(COMMAND(num++, "Page Down", commandButton, cmdPageDown, (quint8)0x0));
    commands.append(COMMAND(num++, "Page Up", commandButton, cmdPageUp, (quint8)0x0));

    commands.append(COMMAND(num++, "AF Gain", commandKnob, cmdSetAfGain, cmdGetAfGain, (quint8)0xff));
    commands.append(COMMAND(num++, "RF Gain", commandKnob, cmdSetRxRfGain, cmdGetRxGain,  (quint8)0xff));
    commands.append(COMMAND(num++, "TX Power", commandKnob, cmdSetTxPower, cmdGetTxPower, (quint8)0xff));
    commands.append(COMMAND(num++, "Mic Gain", commandKnob, cmdSetMicGain, cmdGetMicGain, (quint8)0xff));
    commands.append(COMMAND(num++, "Mod Level", commandKnob, cmdSetModLevel, cmdNone, (quint8)0xff));
    commands.append(COMMAND(num++, "Squelch", commandKnob, cmdSetSql, cmdGetSql, (quint8)0xff));
    commands.append(COMMAND(num++, "Monitor", commandKnob, cmdSetMonitorGain, cmdGetMonitorGain, (quint8)0xff));
    commands.append(COMMAND(num++, "Compressor", commandKnob, cmdSetCompLevel, cmdGetCompLevel, (quint8)0xff));
    commands.append(COMMAND(num++, "Vox Level", commandKnob, cmdSetVoxGain, cmdGetVoxGain, (quint8)0xff));
    commands.append(COMMAND(num++, "Anti-Vox", commandKnob, cmdSetAntiVoxGain, cmdGetAntiVoxGain, (quint8)0xff));
    commands.append(COMMAND(num++, "NB Level", commandKnob, cmdSetNBLevel, cmdGetNBLevel, (quint8)0xff));
    commands.append(COMMAND(num++, "NR Level", commandKnob, cmdSetNRLevel, cmdGetNRLevel, (quint8)0xff));
    commands.append(COMMAND(num++, "Span/Step", commandKnob, cmdSeparator, (quint8)0x0));
    commands.append(COMMAND(num++, "IF Shift", commandKnob, cmdSetIFShift, cmdGetIFShift, (quint8)0xff));
    commands.append(COMMAND(num++, "In PBT", commandKnob, cmdSetTPBFInner, cmdGetTPBFInner, (quint8)0xff));
    commands.append(COMMAND(num++, "Out PBT", commandKnob, cmdSetTPBFOuter, cmdGetTPBFOuter, (quint8)0xff));
    commands.append(COMMAND(num++, "Span/Step", commandKnob, cmdSeparator, (quint8)0x0));
    commands.append(COMMAND(num++, "CW Pitch", commandKnob, cmdSetCwPitch, cmdGetCwPitch, (quint8)0xff));
    commands.append(COMMAND(num++, "CW Speed", commandKnob, cmdSetKeySpeed, cmdGetKeySpeed, (quint8)0xff));
}


void usbController::programPages(USBDEVICE* dev, int val)
{
    QMutexLocker locker(mutex);
    
    if (dev->pages > val) {
        qInfo(logUsbControl()) << "Removing unused pages from " << dev->product;
        // Remove unneded pages

        // Remove old buttons
        for (auto b = buttonList->begin();b != buttonList->end();)
        {
            if (b->parent == dev && b->page > val)
            {
                if (b->text != Q_NULLPTR) {
                    delete b->text;
                    b->text = Q_NULLPTR;
                }
                if (b->bgRect != Q_NULLPTR) {
                    delete b->bgRect;
                    b->bgRect = Q_NULLPTR;
                }
                b->onCommand = Q_NULLPTR;
                b->offCommand = Q_NULLPTR;
                if (b->icon != Q_NULLPTR) {
                    delete b->icon;
                    b->icon=Q_NULLPTR;
                }
                b = buttonList->erase(b);
            } else {
                ++b;
            }
        }

        // Remove old knobs
        for (auto k = knobList->begin();k != knobList->end();)
        {
            if (k->parent == dev && k->page > val)
            {
                if (k->text != Q_NULLPTR) {
                    delete k->text;
                    k->text = Q_NULLPTR;
                    k->command = Q_NULLPTR;
                }
                k = knobList->erase(k);
            } else {
                ++k;
            }
        }
    }
    else if (dev->pages < val)
    {
        for (int page=dev->pages+1; page<=val; page++)
        {
            qInfo(logUsbControl()) << QString("Adding new page %0 to %1").arg(page).arg(dev->product);
            // Add new pages

            for (auto bt = defaultButtons.begin();bt != defaultButtons.end(); bt++)
            {
                if (bt->dev == dev->type.model) {
                    BUTTON but = BUTTON(bt->dev, bt->num, bt->pos, bt->textColour, &commands[0], &commands[0],bt->graphics);
                    but.path = dev->path;
                    but.parent = dev;
                    but.page = page;
                    buttonList->append(but);
                }
            }

            for (auto kb = defaultKnobs.begin();kb != defaultKnobs.end(); kb++)
            {
                if (kb->dev == dev->type.model) {
                    KNOB knob = KNOB(kb->dev, kb->num, kb->pos, kb->textColour, &commands[0]);
                    knob.path = dev->path;
                    knob.parent = dev;
                    knob.page = page;
                    knobList->append(knob);
                }
            }
        }
    }
    dev->pages = val;
}



/* Functions below are for Gamepad controllers */
void usbController::buttonState(QString name, bool val)
{
    Q_UNUSED(name)
    Q_UNUSED(val)
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

void usbController::receiveLevel(cmds cmd, unsigned char level)
{
    // Update knob if relevant, step through all devices
    QMutexLocker locker(mutex);

    for (auto devIt = devices->begin(); devIt != devices->end(); devIt++)
    {
        auto dev = &devIt.value();

        auto kb = std::find_if(knobList->begin(), knobList->end(), [dev, cmd](const KNOB& k)
                               { return (k.command && dev->connected && k.path == dev->path && k.page == dev->currentPage && k.command->getCommand == cmd);});
        if (kb != knobList->end() && kb->num < dev->knobValues.size()) {
            qInfo(logUsbControl()) << "Received value:" << level << "for knob" << kb->num;
            // Set both current and previous knobvalue to the received value
            dev->knobValues[kb->num].value = level/dev->sensitivity;
            dev->knobValues[kb->num].previous = level/dev->sensitivity;
        }
        auto bt = std::find_if(buttonList->begin(), buttonList->end(), [dev, cmd](const BUTTON& b)
                               { return (b.onCommand && dev->connected && b.path == dev->path && b.page == dev->currentPage && b.onCommand->getCommand == cmd && b.led != 0 &&  b.led <= dev->type.leds);});
        if (bt != buttonList->end()) {
            qInfo(logUsbControl()) << "Received value:" << level << "for led" << bt->led;
            QTimer::singleShot(0, this, [=]() { sendRequest(dev,usbFeatureType::featureLEDControl,bt->led,QString("%1").arg(level)); });
        }
    }
}

void usbController::backupController(USBDEVICE* dev, QString file)
{
    QMutexLocker locker(mutex);

    QSettings* settings = new QSettings(file, QSettings::Format::IniFormat);

    qInfo(logUsbControl()) << "Backup of" << dev->path << "to" << file;

    settings->setValue("Version", QString(WFVIEW_VERSION));
    settings->beginGroup("Controller");

    settings->setValue("Model",dev->product);
    settings->setValue("Disabled", dev->disabled);
    settings->setValue("Sensitivity", dev->sensitivity);
    settings->setValue("Brightness", dev->brightness);
    settings->setValue("Orientation", dev->orientation);
    settings->setValue("Speed", dev->speed);
    settings->setValue("Timeout", dev->timeout);
    settings->setValue("Pages", dev->pages);
    settings->setValue("Color", dev->color.name(QColor::HexArgb));
    settings->setValue("LCD", dev->lcd);

    int n=0;
    settings->beginWriteArray("Buttons");
    for (auto b = buttonList->begin(); b != buttonList->end(); b++)
    {
        if (b->path == dev->path)
        {
            settings->setArrayIndex(n);
            settings->setValue("Page", b->page);
            settings->setValue("Dev", b->dev);
            settings->setValue("Num", b->num);
            settings->setValue("Name", b->name);
            settings->setValue("Left", b->pos.left());
            settings->setValue("Top", b->pos.top());
            settings->setValue("Width", b->pos.width());
            settings->setValue("Height", b->pos.height());
            settings->setValue("Colour", b->textColour.name(QColor::HexArgb));
            settings->setValue("BackgroundOn", b->backgroundOn.name(QColor::HexArgb));
            settings->setValue("BackgroundOff", b->backgroundOff.name(QColor::HexArgb));
            if (b->icon != Q_NULLPTR) {
                settings->setValue("Icon", *b->icon);
                settings->setValue("IconName", b->iconName);
            }
            settings->setValue("Toggle", b->toggle);

            if (b->onCommand != Q_NULLPTR)
                settings->setValue("OnCommand", b->onCommand->text);
            if (b->offCommand != Q_NULLPTR)
                settings->setValue("OffCommand", b->offCommand->text);
            settings->setValue("Graphics",b->graphics);
            if (b->led > -1) {
                settings->setValue("Led", b->led);
            }
            ++n;
        }
    }
    settings->endArray();

    n = 0;
    settings->beginWriteArray("Knobs");
    for (auto k = knobList->begin(); k != knobList->end(); k++)
    {
        if (k->path == dev->path)
        {
            settings->setArrayIndex(n);
            settings->setValue("Page", k->page);
            settings->setValue("Dev", k->dev);
            settings->setValue("Num", k->num);
            settings->setValue("Left", k->pos.left());
            settings->setValue("Top", k->pos.top());
            settings->setValue("Width", k->pos.width());
            settings->setValue("Height", k->pos.height());
            settings->setValue("Colour", k->textColour.name());
            if (k->command != Q_NULLPTR)
                settings->setValue("Command", k->command->text);
            ++n;
        }
    }

    settings->endArray();
    settings->endGroup();
    settings->sync();
    delete settings;
}

void usbController::restoreController(USBDEVICE* dev, QString file)
{

    // Signal UI to remove existing device.
    emit removeDevice(dev);

    QMutexLocker locker(mutex);

    QSettings* settings = new QSettings(file, QSettings::Format::IniFormat);

    settings->beginGroup("Controller");

    dev->disabled = settings->value("Disabled", false).toBool();
    dev->sensitivity = settings->value("Sensitivity", 1).toInt();
    dev->pages = settings->value("Pages", 1).toInt();
    dev->brightness = (quint8)settings->value("Brightness", 2).toInt();
    dev->orientation = (quint8)settings->value("Orientation", 2).toInt();
    dev->speed = (quint8)settings->value("Speed", 2).toInt();
    dev->timeout = (quint8)settings->value("Timeout", 30).toInt();
    dev->color.setNamedColor(settings->value("Color", QColor(Qt::white).name(QColor::HexArgb)).toString());
    dev->lcd = (cmds)settings->value("LCD",0).toInt();

    qInfo(logUsbControl()) << "Restore of" << dev->product << "path" << dev->path << "from" << file;

    // Remove old buttons

    for (auto b = buttonList->begin();b != buttonList->end();)
    {
        if (b->parent == dev)
        {
            if (b->text != Q_NULLPTR) {
                delete b->text;
                b->text = Q_NULLPTR;
            }
            if (b->bgRect != Q_NULLPTR) {
                delete b->bgRect;
                b->bgRect = Q_NULLPTR;
            }
            b->onCommand = Q_NULLPTR;
            b->offCommand = Q_NULLPTR;
            if (b->icon != Q_NULLPTR) {
                delete b->icon;
                b->icon=Q_NULLPTR;
            }
            b = buttonList->erase(b);
        } else {
            ++b;
        }
    }

    int numButtons = settings->beginReadArray("Buttons");
    if (numButtons == 0) {
        qInfo(logUsbControl()) << "No Buttons Found!";
        settings->endArray();
    }
    else {
        for (int b = 0; b < numButtons; b++)
        {
            settings->setArrayIndex(b);
            BUTTON but;
            but.page = settings->value("Page", 1).toInt();
            but.dev = (usbDeviceType)settings->value("Dev", 0).toInt();
            but.num = settings->value("Num", 0).toInt();
            but.name = settings->value("Name", "").toString();
            but.pos = QRect(settings->value("Left", 0).toInt(),
                settings->value("Top", 0).toInt(),
                settings->value("Width", 0).toInt(),
                settings->value("Height", 0).toInt());
            but.textColour.setNamedColor(settings->value("Colour", QColor(Qt::white).name(QColor::HexArgb)).toString());
            but.backgroundOn.setNamedColor(settings->value("BackgroundOn", QColor(Qt::lightGray).name(QColor::HexArgb)).toString());
            but.backgroundOff.setNamedColor(settings->value("BackgroundOff", QColor(Qt::blue).name(QColor::HexArgb)).toString());
            but.toggle = settings->value("Toggle", false).toBool();
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0))
            if (settings->value("Icon",NULL) != NULL) {
                but.icon = new QImage(settings->value("Icon",NULL).value<QImage>());
                but.iconName = settings->value("IconName", "").toString();
            }
#endif
            but.on = settings->value("OnCommand", "None").toString();
            but.off = settings->value("OffCommand", "None").toString();
            but.graphics = settings->value("Graphics",false).toBool();
            but.led = settings->value("Led", -1).toInt();
            but.path = dev->path;
            qInfo(logUsbControl()) << "Restoring button" << but.num << "On" << but.on << "Off" << but.off;
            buttonList->append(BUTTON(but));
        }
        settings->endArray();
    }


    // Remove old knobs

    for (auto k = knobList->begin();k != knobList->end();)
    {
        if (k->parent == dev)
        {
            if (k->text != Q_NULLPTR) {
                delete k->text;
                k->text = Q_NULLPTR;
                k->command = Q_NULLPTR;
            }
            k = knobList->erase(k);
        } else {
            ++k;
        }
    }

    int numKnobs = settings->beginReadArray("Knobs");
    if (numKnobs == 0) {
        qInfo(logUsbControl()) << "No Knobs Found!";
        settings->endArray();
    }
    else {
        for (int k = 0; k < numKnobs; k++)
        {
            settings->setArrayIndex(k);
            KNOB kb;
            kb.page = settings->value("Page", 1).toInt();
            kb.dev = (usbDeviceType)settings->value("Dev", 0).toInt();
            kb.num = settings->value("Num", 0).toInt();
            kb.name = settings->value("Name", "").toString();
            kb.pos = QRect(settings->value("Left", 0).toInt(),
                settings->value("Top", 0).toInt(),
                settings->value("Width", 0).toInt(),
                settings->value("Height", 0).toInt());
            kb.textColour = QColor((settings->value("Colour", "Green").toString()));

            kb.cmd = settings->value("Command", "None").toString();
            kb.path = dev->path;
            qInfo(logUsbControl()) << "Restoring knob" << kb.num << "Cmd" << kb.cmd;
            knobList->append(KNOB(kb));
        }
        settings->endArray();
    }

    settings->endGroup();
    settings->sync();
    delete settings;

    qInfo(logUsbControl()) << "Disconnecting device" << dev->product;
    hid_close(dev->handle);
    dev->handle = NULL;
    dev->connected = false;
    dev->uiCreated = false;
    devicesConnected--;
    QTimer::singleShot(250, this, SLOT(run())); // Call run to cleanup connectons after 250ms
}

#endif
