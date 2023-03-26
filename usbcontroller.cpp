
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
    knownDevices.append(USBTYPE(shuttleXpress,0x0b33,0x0020,0x0000,0x0000,15,2,5,0));
    knownDevices.append(USBTYPE(shuttlePro2,0x0b33,0x0030,0x0000,0x0000,15,2,5,0));
    knownDevices.append(USBTYPE(shuttlePro2,0x0b33,0x0011,0x0000,0x0000,15,2,5,0)); // Actually a ShuttlePro but hopefully will work?
    knownDevices.append(USBTYPE(RC28,0x0c26,0x001e,0x0000,0x0000,3,1,64,0));
    knownDevices.append(USBTYPE(eCoderPlus, 0x1fc9, 0x0003,0x0000,0x0000,16,4,32,0));
    knownDevices.append(USBTYPE(QuickKeys, 0x28bd, 0x5202,0x0001,0xff0a,10,1,32,0));
    knownDevices.append(USBTYPE(StreamDeckMini, 0x0fd9, 0x0063, 0x0000, 0x0000,6,0,1024,80));
    knownDevices.append(USBTYPE(StreamDeckMiniV2, 0x0fd9, 0x0090, 0x0000, 0x0000,6,0,1024,80));
    knownDevices.append(USBTYPE(StreamDeckOriginal, 0x0fd9, 0x0060, 0x0000, 0x0000,15,0,8191,72));
    knownDevices.append(USBTYPE(StreamDeckOriginalV2, 0x0fd9, 0x006d, 0x0000, 0x0000,15,0,1024,72));
    knownDevices.append(USBTYPE(StreamDeckOriginalMK2, 0x0fd9, 0x0080, 0x0000, 0x0000,15,0,1024,72));
    knownDevices.append(USBTYPE(StreamDeckXL, 0x0fd9, 0x006c, 0x0000, 0x0000,32,0,1024,96));
    knownDevices.append(USBTYPE(StreamDeckXLV2, 0x0fd9, 0x008f, 0x0000, 0x0000,32,0,1024,96));
    knownDevices.append(USBTYPE(StreamDeckPedal, 0x0fd9, 0x0086, 0x0000, 0x0000,3,0,1024,0));
    knownDevices.append(USBTYPE(StreamDeckPlus, 0x0fd9, 0x0084, 0x0000, 0x0000,12,4,1024,120));
}

usbController::~usbController()
{
    qInfo(logUsbControl) << "Ending usbController()";
    
    for (USBDEVICE &dev: usbDevices)
    {
        if (dev.handle) {
            sendRequest(&dev,usbFeatureType::featureOverlay,60,"Goodbye from wfview");

            if (dev.type.model == RC28) {
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
    
    emit initUI();
    
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
                newdev.type.model = xBoxGamepad;
            }
            else {
                newdev.type.model = unknownGamepad;
            }
            
            newDev.product = gamepad->name();
            newDev.path = gamepad->name();
            // Is this a new device? If so add it to usbDevices
            // auto p = std::find_if(usbDevices.begin(),usbDevices.end(),[newDev](const USBDEVICE& dev) {return dev.path == newDev.path; });
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
        if (it.value().remove)
        {
            it = usbDevices.erase(it);
        }
        else
        {
            ++it;
        }
    }
#else
    // Remove any devices from the list that are not connected (doesn't work on QT5!)
    usbDevices.erase(std::remove_if(usbDevices.begin(), usbDevices.end(), [](const USBDEVICE& dev) { return (dev.remove); }),usbDevices.end());
#endif
    struct hid_device_info* devs;
    devs = hid_enumerate(0x0, 0x0);
    
    while (devs) {
        for (auto i = knownDevices.begin();i != knownDevices.end(); i++)
        {
            if (devs->vendor_id == i->manufacturerId && devs->product_id == i->productId
                    && (i->usage == 0x00 || devs->usage == i->usage)
                    && (i->usagePage == 0x00 || devs->usage_page == i->usagePage))
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
                // Is this a new device? If so add it to usbDevices
                
                if (!usbDevices.contains(newDev.path))
                {
                    usbDevices.insert(newDev.path,newDev);
                }
            }
        }
        devs = devs->next;
    }
    
    hid_free_enumeration(devs);
    
    for (USBDEVICE &dev: usbDevices) {
        
        dev.disabled = (*controllers)[dev.path].disabled;
        dev.pages = (*controllers)[dev.path].pages;
        if (!dev.disabled && !dev.connected && !dev.path.isEmpty()) {
            
            if (!controllers->contains(dev.path)) {
                controllers->insert(dev.path,CONTROLLER(&dev));
            } else {
                (*controllers)[dev.path].dev = &dev;
            }
            
            qInfo(logUsbControl()) << QString("Attempting to connect to %0").arg(dev.product);
            dev.handle = hid_open_path(dev.path.toLocal8Bit());
            
            if (dev.handle)
            {
                qInfo(logUsbControl()) << QString("Connected to device: %0 from %1 S/N %2").arg(dev.product).arg(dev.manufacturer).arg(dev.serial);
                hid_set_nonblocking(dev.handle, 1);
                devicesConnected++;
                dev.connected=true;

                locker.unlock(); // Unlock the mutex so other devices can use it

                if (dev.type.model == RC28)
                {
                    ledControl(false, 0);
                    ledControl(false, 1);
                    ledControl(false, 2);
                    ledControl(true, 3);
                }
                else if (dev.type.model == QuickKeys)
                {
                    sendRequest(&dev,usbFeatureType::featureEventsA);
                    sendRequest(&dev,usbFeatureType::featureEventsB);
                }

                sendRequest(&dev,usbFeatureType::featureSerial);
                sendRequest(&dev,usbFeatureType::featureFirmware);
                sendRequest(&dev,usbFeatureType::featureResetKeys);
                //sendRequest(&dev,usbFeatureType::featureReset);
                sendRequest(&dev,usbFeatureType::featureOverlay,5,"Hello from wfview");
                locker.relock(); // Take the mutex back.
            }
            else if (!dev.uiCreated)
            {
                // This should only get displayed once if we fail to connect to a device
                qInfo(logUsbControl()) << QString("Error connecting to  %0: %1")
                                          .arg(dev.product)
                                          .arg(QString::fromWCharArray(hid_error(dev.handle)));
            }
            
            if (!dev.uiCreated)
            {
                for (int i=0;i<dev.type.knobs;i++)
                {
                    dev.knobValues.append(0);
                    dev.knobSend.append(0);
                    dev.knobPrevious.append(0);
                }

                // Find our defaults/knobs/buttons for this controller:
                // First see if we have any stored and add them to the list if not.
                
                auto bti = std::find_if(buttonList->begin(), buttonList->end(), [dev](const BUTTON& b)
                { return (b.path == dev.path); });
                if (bti == buttonList->end())
                {
                    // List doesn't contain any buttons for this device so add all existing buttons to the end of buttonList
                    qInfo(logUsbControl()) << "No stored buttons found, loading defaults";
                    for (auto but=defaultButtons.begin();but!=defaultButtons.end();but++)
                    {
                        if (but->dev == dev.type.model)
                        {
                            but->path = dev.path;
                            buttonList->append(BUTTON(*but));
                        }
                    }
                }

                // We need to set the parent device for all buttons belonging to this device!
                for (auto but = buttonList->begin(); but != buttonList->end(); but++)
                {
                    if (but->path == dev.path)
                    {
                        auto bon = std::find_if(commands.begin(), commands.end(), [but](const COMMAND& c) { return (c.text == but->on); });
                        if (bon != commands.end())
                            but->onCommand = new COMMAND(*bon);
                        else
                            qWarning(logUsbControl()) << "On Command" << but->on << "not found";

                        auto boff = std::find_if(commands.begin(), commands.end(), [but](const COMMAND& c) { return (c.text == but->off); });
                        if (boff != commands.end())
                            but->offCommand = new COMMAND(*boff);
                        else
                            qWarning(logUsbControl()) << "Off Command" << but->on << "not found";

                        but->parent = &dev;
                    }
                }

                
                auto kbi = std::find_if(knobList->begin(), knobList->end(), [dev](const KNOB& k)
                { return (k.path == dev.path); });
                if (kbi == knobList->end())
                {
                    qInfo(logUsbControl()) << "No stored knobs found, loading defaults";
                    for (auto kb = defaultKnobs.begin();kb != defaultKnobs.end(); kb++)
                    {
                        if (kb->dev == dev.type.model) {
                            kb->path = dev.path;
                            knobList->append(KNOB(*kb));
                        }
                    }
                }

                for (auto kb = knobList->begin(); kb != knobList->end(); kb++)
                {
                    if (kb->path == dev.path)
                    {
                        auto k = std::find_if(commands.begin(), commands.end(), [kb](const COMMAND& c) { return (c.text == kb->cmd); });
                        if (k != commands.end())
                            kb->command = new COMMAND(*k);
                        else
                            qWarning(logUsbControl()) << "Knob Command" << kb->cmd << "not found";

                        kb->parent = &dev;

                    }
                }

                // Let the UI know we have a new controller
                emit newDevice(&dev, &(*controllers)[dev.path],buttonList, knobList, &commands, mutex);
                
                dev.uiCreated = true;
            }
        }
        else if (dev.uiCreated)
        {
            emit setConnected(&dev);
        }
    }
    
    if (devicesConnected>0) {
        // Run the periodic timer to get data if we have any devices
        QTimer::singleShot(25, this, SLOT(runTimer()));
    }
    
    // Run the periodic timer to check for new devices
    QTimer::singleShot(2000, this, SLOT(run()));
    
}


/*
 * runTimer is called every 25ms once a connection to a supported controller is established
 * It will then step through each connected controller and request any data
*/

void usbController::runTimer()
{
    QMutexLocker locker(mutex);
    
    for (USBDEVICE &dev: usbDevices)
    {
        if (dev.disabled || !dev.connected || !dev.handle) {
            continue;
        }
        
        int res=1;
        
        while (res > 0) {
            quint32 tempButtons = 0;
            QByteArray data(HIDDATALENGTH, 0x0);
            res = hid_read(dev.handle, (unsigned char*)data.data(), HIDDATALENGTH);
            if (res < 0)
            {
                qInfo(logUsbControl()) << "USB Device disconnected" << dev.product;
                emit removeDevice(&dev);
                hid_close(dev.handle);
                dev.handle = NULL;
                dev.connected = false;
                dev.remove = true;
                devicesConnected--;
                break;
            }

            if (res == 5 && (dev.type.model == shuttleXpress || dev.type.model == shuttlePro2))
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

                if (tempJogpos == dev.jogpos + 1 || (tempJogpos == 0 && dev.jogpos == 0xff))
                {
                    dev.knobValues[0]++;
                }
                else if (tempJogpos != dev.jogpos) {
                    dev.knobValues[0]--;
                }

                dev.jogpos = tempJogpos;
                dev.shutpos = tempShutpos;
            }
            else if ((res > 31) && dev.type.model == RC28)
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
                            dev.knobValues[0] = dev.knobValues[0] + data[1];
                        }
                        else if ((unsigned char)data[3] == 0x02)
                        {
                            dev.knobValues[0] = dev.knobValues[0] - data[1];
                        }
                    }
                }
            }
            else if (res > 15 && dev.type.model == eCoderPlus && (quint8)data[0] == 0xff) {
                tempButtons = ((quint8)data[3] << 16) | ((quint8)data[2] << 8) | ((quint8)data[1] & 0xff);
                quint32 tempKnobs = ((quint8)data[16] << 24) | ((quint8)data[15] << 16) | ((quint8)data[14] << 8) | ((quint8)data[13]  & 0xff);
                
                for (unsigned char i = 0; i < dev.knobValues.size(); i++)
                {
                    if (dev.knobs != tempKnobs) {
                        // One of the knobs has moved
                        for (unsigned char i = 0; i < 4; i++) {
                            if ((tempKnobs >> (i * 8) & 0xff) != (dev.knobs >> (i * 8) & 0xff)) {
                                dev.knobValues[i] = dev.knobValues[i] + (qint8)((dev.knobs >> (i * 8)) & 0xff);
                            }
                        }
                        dev.knobs = tempKnobs;
                    }
                }
            }
            else if (res > 5 && dev.type.model == QuickKeys && (quint8)data[0] == 0x02) {

                if ((quint8)data[1] == 0xf0) {
                    
                    //qInfo(logUsbControl()) << "Received:" << data;
                    tempButtons = (data[3] << 8) | (data[2] & 0xff);

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
            // Is it any model of StreamDeck?
            else if (res>=dev.type.buttons && dev.type.model != usbNone)
            {
                // Main buttons
                if ((quint8)data[1] == 0x00)
                {
                    for (int i=dev.type.buttons-dev.type.knobs;i>0;i--) {
                        tempButtons |= ((quint8)data[i+3] & 0x01) << (i-1);
                    }
                }

                // Knobs and secondary buttons
                if (dev.type.model == StreamDeckPlus) {
                    if ((quint8)data[1] == 0x03 && (quint8)data[2] == 0x05)
                    {
                        // Knob action!
                        switch ((quint8)data[4])
                        {
                        case 0x00:
                            // Knob button
                            for (int i=dev.type.buttons;i>7;i--)
                            {
                                tempButtons |= ((quint8)data[i-4] & 0x01) << (i-1);
                            }
                            break;
                        case 0x01:
                            // Knob moved
                            for (int i=0;i<dev.type.knobs;i++)
                            {
                                dev.knobValues[i] += (qint8)data[i+5];
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

            // Step through all buttons and emit ones that have been pressed.
            // Only do it if actual data has been received.
            if (res > 0 && dev.buttons != tempButtons)
            {
                qInfo(logUsbControl()) << "Got Buttons:" << QString::number(tempButtons,2);
                // Step through all buttons and emit ones that have been pressed.
                for (unsigned char i = 0; i <dev.type.buttons; i++)
                {
                    auto but = std::find_if(buttonList->begin(), buttonList->end(), [dev, i](const BUTTON& b)
                    { return (b.path == dev.path && b.page == dev.currentPage && b.num == i); });
                    if (but != buttonList->end()) {
                        if ((!but->isOn) && ((tempButtons >> i & 1) && !(dev.buttons >> i & 1)))
                        {
                            qDebug(logUsbControl()) << QString("On Button event for button %0: %1").arg(but->num).arg(but->onCommand->text);
                            if (but->onCommand->command == cmdPageUp)
                                emit changePage(&dev, dev.currentPage+1);
                            else if (but->onCommand->command == cmdPageDown)
                                emit changePage(&dev, dev.currentPage-1);
                            else if (but->onCommand->command == cmdLCDSpectrum)
                                (*controllers)[dev.path].lcd = cmdLCDSpectrum;
                            else if (but->onCommand->command == cmdLCDWaterfall)
                                (*controllers)[dev.path].lcd = cmdLCDWaterfall;
                            else {
                                emit button(but->onCommand);
                            }
                            // Change the button text to reflect the off Button
                            if (but->offCommand->index != 0) {
                                locker.unlock();
                                sendRequest(&dev,usbFeatureType::featureButton,i,but->offCommand->text,but->icon,&but->backgroundOff);
                                locker.relock();
                            }
                            but->isOn=true;
                        }
                        else if ((but->toggle && but->isOn) && ((tempButtons >> i & 1) && !(dev.buttons >> i & 1)))
                        {
                            qDebug(logUsbControl()) << QString("Off Button (toggle) event for button %0: %1").arg(but->num).arg(but->onCommand->text);
                            if (but->offCommand->command == cmdPageUp)
                                emit changePage(&dev, dev.currentPage+1);
                            else if (but->offCommand->command == cmdPageDown)
                                emit changePage(&dev, dev.currentPage-1);
                            else if (but->offCommand->command == cmdLCDSpectrum)
                                (*controllers)[dev.path].lcd = cmdLCDSpectrum;
                            else if (but->offCommand->command == cmdLCDWaterfall)
                                (*controllers)[dev.path].lcd = cmdLCDWaterfall;
                            else {
                                emit button(but->offCommand);
                            }
                            locker.unlock();
                            sendRequest(&dev,usbFeatureType::featureButton,i,but->onCommand->text,but->icon,&but->backgroundOn);
                            locker.relock();
                            but->isOn=false;
                        }
                        else if ((!but->toggle && but->isOn) && ((dev.buttons >> i & 1) && !(tempButtons >> i & 1)))
                        {
                            if (but->offCommand->command == cmdLCDSpectrum)
                                (*controllers)[dev.path].lcd = cmdLCDSpectrum;
                            else if (but->offCommand->command == cmdLCDWaterfall)
                                (*controllers)[dev.path].lcd = cmdLCDWaterfall;
                            else
                            {
                                qDebug(logUsbControl()) << QString("Off Button event for button %0: %1").arg(but->num).arg(but->offCommand->text);
                                emit button(but->offCommand);
                            }
                            // Change the button text to reflect the on Button
                            locker.unlock();
                            sendRequest(&dev,usbFeatureType::featureButton,i,but->onCommand->text,but->icon,&but->backgroundOn);
                            locker.relock();
                            but->isOn=false;
                        }
                    }
                }
                dev.buttons = tempButtons;
            }

            // As the knobs can move very fast, only send every 100ms so the rig isn't overwhelmed
            if (dev.lastusbController.msecsTo(QTime::currentTime()) >= 100 || dev.lastusbController > QTime::currentTime())
            {
                if (dev.type.model == shuttleXpress || dev.type.model == shuttlePro2)
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
                
                for (unsigned char i = 0; i < dev.knobValues.size(); i++)
                {
                    auto kb = std::find_if(knobList->begin(), knobList->end(), [dev, i](const KNOB& k)
                    { return (k.command && k.path == dev.path && k.page == dev.currentPage && k.num == i && dev.knobValues[i] != dev.knobPrevious[i]); });

                    if (kb != knobList->end()) {
                        // sendCommand mustn't be deleted so we ensure it stays in-scope by declaring it private (we will only ever send one command).
                        sendCommand = *kb->command;
                        if (sendCommand.command != cmdSetFreq) {
                            int tempVal = dev.knobValues[i] * dev.sensitivity;
                            tempVal = qMin(qMax(tempVal,0),255);
                            sendCommand.suffix = quint8(tempVal);
                            dev.knobValues[i]=tempVal/dev.sensitivity; // This ensures that dial can't go outside 0-255
                        }
                        else
                        {
                            sendCommand.value = dev.knobValues[i]/dev.sensitivity;
                        }
                        
                        emit button(&sendCommand);
                        
                        if (sendCommand.command == cmdSetFreq) {
                            dev.knobValues[i] = 0;
                        }
                        dev.knobPrevious[i]=dev.knobValues[i];
                    }
                }
                
                dev.lastusbController = QTime::currentTime();
            }
            
        }
    }
    
    if (devicesConnected>0) {
        // Run the periodic timer to get data if we have any devices
        QTimer::singleShot(25, this, SLOT(runTimer()));
    }
}

void usbController::receivePTTStatus(bool on) {
    static QColor lastColour = currentColour;
    static bool ptt;
    QMutexLocker locker(mutex);

    for (USBDEVICE &dev: usbDevices) {
        locker.unlock();
        if (on && !ptt) {
            lastColour = currentColour;
            sendRequest(&dev,usbFeatureType::featureColor,0,QColor(255,0,0).name(QColor::HexArgb));
        }
        else {
            sendRequest(&dev,usbFeatureType::featureColor,0,lastColour.name(QColor::HexArgb));
        }
        locker.relock();
    }

    ptt = on;
}

void usbController::sendToLCD(QImage* img)
{

    for (USBDEVICE &dev: usbDevices)
    {
        sendRequest(&dev,usbFeatureType::featureLCD,0,"",img);
    }
}

/*
 * All functions below here are for specific controllers
*/

void usbController::ledControl(bool on, unsigned char num)
{
    // Currently this will act on ALL connected RC28 devices
    // Not sure if this needs to change?
    
    QMutexLocker locker(mutex);

    for (USBDEVICE &dev: usbDevices) {
        if (dev.connected && dev.type.model == RC28) {
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

/* This function will handle various commands for multiple models of controller
 *
 * Possible features are:
 * featureInit, featureFirmware, featureSerial, featureButton, featureSensitivity, featureBrightness,
 * featureOrientation, featureSpeed, featureColor, featureOverlay, featureTimeout, featurePages, featureDisable
*/

void usbController::sendRequest(USBDEVICE *dev, usbFeatureType feature, quint8 val, QString text, QImage* img, QColor* color)
{
    if (dev == Q_NULLPTR || !dev->connected || dev->disabled || !dev->handle)
        return;

    QMutexLocker locker(mutex);

    QByteArray data(64, 0x0);
    QByteArray data2;
    int res=0;
    bool sdv1=false;
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
        case usbFeatureType::featureSensitivity:
            dev->sensitivity=val;
            (*controllers)[dev->path].sensitivity = val;
            return;
            break;
        case usbFeatureType::featureBrightness:
            qDebug(logUsbControl()) << QString("Setting brightness to %0").arg(val);
            data[1] = (qint8)0xb1;
            data[2] = (qint8)0x0a;
            data[3] = (qint8)0x01;
            data[4] = val;
            (*controllers)[dev->path].brightness = val;
            break;
        case usbFeatureType::featureOrientation:
            data[1] = (qint8)0xb1;
            data[2] = (qint8)val+1;
            (*controllers)[dev->path].orientation = val;
            break;
        case usbFeatureType::featureSpeed:
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x04;
            data[3] = (qint8)0x01;
            data[4] = (qint8)0x01;
            data[5] = val+1;
            (*controllers)[dev->path].speed = val;
            break;
        case usbFeatureType::featureColor: {
            QColor col;
            col.setNamedColor(text);
            data[1] = (qint8)0xb4;
            data[2] = (qint8)0x01;
            data[3] = (qint8)0x01;
            data[6] = (qint8)col.red();
            data[7] = (qint8)col.green();
            data[8] = (qint8)col.blue();
            if (val) {
               currentColour = col;
               (*controllers)[dev->path].color=currentColour;
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
            (*controllers)[dev->path].timeout = val;
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
        // Allow pass through.
        // Below are StreamDeck Generation 2 h/w
    case StreamDeckOriginalMK2:
    case StreamDeckOriginalV2:
    case StreamDeckXL:
    case StreamDeckXLV2:
    case StreamDeckPlus:
    case StreamDeckPedal:
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
            qInfo(logUsbControl()) << QString("%0: Firmware = %1").arg(dev->product).arg(QString::fromLatin1(data.mid(2,12)));
            break;
        case usbFeatureType::featureSerial:
            if (sdv1) {
                data[0] = 0x03;
            } else {
                data[0] = 0x06;
            }
            hid_get_feature_report(dev->handle,(unsigned char*)data.data(),(size_t)data.size());
            qInfo(logUsbControl()) << QString("%0: Serial Number = %1").arg(dev->product).arg(QString::fromLatin1(data.mid(5,8)));
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
                data[2] = val*25; // Stream Deck brightness is in %
            }
            res = hid_send_feature_report(dev->handle, (const unsigned char*)data.constData(), data.size());
            break;
        case usbFeatureType::featureColor:
            if (val) {
                currentColour.setNamedColor(text);
                (*controllers)[dev->path].color=currentColour;
            }
            break;
        case usbFeatureType::featureOverlay:
        {
            if (dev->type.model == usbDeviceType::StreamDeckPlus)
            {
                QImage image(800,100, QImage::Format_RGB888);
                QPainter paint(&image);
                if (val) {
                    paint.setFont(QFont("times",16));
                    paint.fillRect(image.rect(), (*controllers)[dev->path].color);
                    paint.drawText(image.rect(),Qt::AlignCenter | Qt::AlignVCenter, text);
                    QTimer::singleShot(val*1000, this, [=]() { sendRequest(dev,usbFeatureType::featureOverlay); });
                } else {
                    paint.fillRect(image.rect(), (*controllers)[dev->path].color);
                }
                QBuffer buffer(&data2);
                image.save(&buffer, "JPG");
            }
            // Fall through
        }
        case usbFeatureType::featureLCD:
        {
            if (dev->type.model == usbDeviceType::StreamDeckPlus)
            {
                if (img != Q_NULLPTR)
                {
                    QImage image = img->scaled(800,100,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
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
                    quint16 length = qMin(rem,dev->type.maxPayload-sizeof(h));
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
                if (val < 8) {
                    QImage butImage(dev->type.iconSize,dev->type.iconSize, QImage::Format_RGB888);
                    if (color != Q_NULLPTR)
                        butImage.fill(*color);
                    else
                        butImage.fill((*controllers)[dev->path].color);

                    QPainter butPaint(&butImage);

                    if ( img == Q_NULLPTR) {
                        butPaint.setFont(QFont("times",16));
                        butPaint.drawText(butImage.rect(),Qt::AlignCenter | Qt::AlignVCenter, text);
                    } else {
                        butPaint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                        butPaint.drawImage(0, 0, *img);
                    }

                    QBuffer butBuffer(&data2);

                    if (sdv1)
                    {
                        butImage.save(&butBuffer, "BMP");
                        quint32 rem = data2.size();
                        quint16 index = 0;
                        streamdeck_v1_image_header h1;
                        memset(h1.packet, 0x0, sizeof(h1)); // We can't be sure it is initialized with 0x00!
                        h1.cmd = 0x02;
                        h1.suffix = 0x01;
                        h1.button = val;
                        while (rem > 0)
                        {
                            quint16 length = qMin(rem,dev->type.maxPayload-sizeof(h1));
                            data.clear();
                            h1.isLast = (quint8)(rem <= dev->type.maxPayload-sizeof(h1) ? 1 : 0); // isLast ? 1 : 0,3
                            h1.index = index;
                            data.append(QByteArray::fromRawData((const char*)h1.packet,sizeof(h1)));
                            rem -= length;
                            data.append(data2.mid(0,length));
                            data.resize(dev->type.maxPayload);
                            memset(data.data()+length+sizeof(h1),0x0,data.size()-(length+sizeof(h1)));
                            res=hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
                            //qInfo(logUsbControl()) << "Sending" << (((quint8)data[7] << 8) | ((quint8)data[6] & 0xff)) << "total=" << data.size()  << "payload=" << (((quint8)data[5] << 8) | ((quint8)data[4] & 0xff)) << "last" << (quint8)data[3];
                            data2.remove(0,length);
                            index++;
                        }
                    }
                    else
                    {
                        butImage.save(&butBuffer, "JPG");
                        quint32 rem = data2.size();
                        quint16 index = 0;
                        streamdeck_image_header h;
                        memset(h.packet, 0x0, sizeof(h)); // We can't be sure it is initialized with 0x00!
                        h.cmd = 0x02;
                        h.suffix = 0x07;
                        h.button = val;
                        while (rem > 0)
                        {
                            quint16 length = qMin(rem,dev->type.maxPayload-sizeof(h));
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
        if (feature == usbFeatureType::featureFirmware)
        {
            data[0] = 63;
            data[1] = 0x02;
            hid_write(dev->handle, (const unsigned char*)data.constData(), data.size());
        }
        break;

    default:
        break;
    }

    if (res == -1)
        qInfo(logUsbControl()) << "Command" << feature << "returned" << res;
}

void usbController::programDisable(USBDEVICE* dev, bool disabled)
{
    dev->disabled = disabled;
    (*controllers)[dev->path].disabled = disabled;
    
    if (disabled)
    {
        // Disconnect the device:
        if (dev->handle) {
            qInfo(logUsbControl()) << "Disconnecting device:" << dev->product;
            sendRequest(dev,usbFeatureType::featureOverlay,60,"Goodbye from wfview");

            QMutexLocker locker(mutex);

            if (dev->type.model == RC28) {
                ledControl(false, 3);
            }
            hid_close(dev->handle);
            dev->connected=false;
            dev->handle=NULL;
        }
    }
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

    // StreamDeckPlus
    defaultButtons.append(BUTTON(StreamDeckPlus, 0, QRect(69, 43, 78, 66), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 1, QRect(198, 43, 78, 66), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 2, QRect(323, 43, 78, 66), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 3, QRect(452, 43, 78, 66), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 4, QRect(69, 126, 78, 66), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 5, QRect(198, 126, 78, 66), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 6, QRect(323, 126, 78, 66), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 7, QRect(452, 126, 78, 66), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 8, QRect(74, 358, 64, 52), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 9, QRect(204, 358, 64, 52), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 10, QRect(332, 358, 64, 52), Qt::white, &commands[0], &commands[0]));
    defaultButtons.append(BUTTON(StreamDeckPlus, 11, QRect(462, 358, 64, 52), Qt::white, &commands[0], &commands[0]));
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

    // StreamDeckPlus
    defaultKnobs.append(KNOB(StreamDeckPlus, 0, QRect(74, 413, 64, 28), Qt::green, &commands[4]));
    defaultKnobs.append(KNOB(StreamDeckPlus, 1, QRect(204, 413, 64, 28), Qt::green, &commands[0]));
    defaultKnobs.append(KNOB(StreamDeckPlus, 2, QRect(332, 413, 64, 28), Qt::green, &commands[0]));
    defaultKnobs.append(KNOB(StreamDeckPlus, 3, QRect(462, 413, 64, 28), Qt::green, &commands[0]));
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
    commands.append(COMMAND(num++, "Waterfall", commandButton, cmdLCDWaterfall, 0x0));
    commands.append(COMMAND(num++, "Spectrum", commandButton, cmdLCDSpectrum, 0x0));
    commands.append(COMMAND(num++, "Page Down", commandButton, cmdPageDown, 0x0));
    commands.append(COMMAND(num++, "Page Up", commandButton, cmdPageUp, 0x0));
}


void usbController::programPages(USBDEVICE* dev, int val)
{
    QMutexLocker locker(mutex);
    
    if (dev->pages > val) {
        qInfo(logUsbControl()) << "Removing unused pages from " << dev->product;
        // Remove unneded pages
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        auto bt = buttonList->begin();
        while (bt != buttonList->end())
        {
            if (bt->devicePath == dev->path && bt->page > val) {
                delete bt->onCommand;
                delete bt->offCommand;
                bt = buttonList->erase(bt);
            }
            else
                ++bt;
        }
        auto kb = knobList->begin();
        while (kb != knobList->end())
        {
            if (kb->devicePath == dev->path && kb->page > val) {
                delete kb->command;
                kb = knobList->erase(kb);
            } else
                ++kb;
        }
#else
        buttonList->erase(std::remove_if(buttonList->begin(), buttonList->end(), [val](auto bt) {
            return (bt.page>val); }),buttonList->end());
        knobList->erase(std::remove_if(knobList->begin(), knobList->end(), [val](auto kb) {
            return (kb.page>val); }),knobList->end());
#endif
    }
    else if (dev->pages < val)
    {
        for (int page=dev->pages+1; page<=val; page++)
        {
            qInfo(logUsbControl()) << QString("Adding new page %0 to %1").arg(page).arg(dev->product);
            // Add new pages
            
            QVector<BUTTON> tempButs;
            auto bt = buttonList->begin();
            while (bt != buttonList->end())
            {
                if (bt->path == dev->path && bt->page == 1) {
                    BUTTON but = BUTTON(bt->dev, bt->num, bt->pos, bt->textColour, new COMMAND(commands[0]), new COMMAND(commands[0]));
                    but.path = dev->path;
                    but.parent = dev;
                    but.page = page;
                    tempButs.append(but);
                }
                ++bt;
            }
            buttonList->append(tempButs);
            
            QVector<KNOB> tempKnobs;
            auto kb = knobList->begin();
            while (kb != knobList->end())
            {
                if (kb->path == dev->path && kb->page == 1) {
                    KNOB knob = KNOB(kb->dev, kb->num, kb->pos, kb->textColour, new COMMAND(commands[0]));
                    knob.path = dev->path;
                    knob.parent = dev;
                    knob.page = page;
                    tempKnobs.append(knob);
                }
                ++kb;
            }
            knobList->append(tempKnobs);
        }
    }
    dev->pages = val;
    (*controllers)[dev->path].pages = val;
}



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


#endif
