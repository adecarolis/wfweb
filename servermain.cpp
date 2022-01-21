#include "servermain.h"

#include "commhandler.h"
#include "rigidentities.h"
#include "logcategories.h"
#include <iostream>

// This code is copyright 2017-2020 Elliott H. Liggett
// All rights reserved

servermain::servermain(const QString serialPortCL, const QString hostCL, const QString settingsFile)
{
    this->serialPortCL = serialPortCL;
    this->hostCL = hostCL;

    qRegisterMetaType<udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType<rigCapabilities>();
    qRegisterMetaType<duplexMode>();
    qRegisterMetaType<rptAccessTxRx>();
    qRegisterMetaType<rigInput>();
    qRegisterMetaType<meterKind>();
    qRegisterMetaType<spectrumMode>();
    qRegisterMetaType<freqt>();
    qRegisterMetaType<mode_info>();
    qRegisterMetaType<audioPacket>();
    qRegisterMetaType <audioSetup>();
    qRegisterMetaType <SERVERCONFIG>();
    qRegisterMetaType <timekind>();
    qRegisterMetaType <datekind>();
    qRegisterMetaType<rigstate*>();
    qRegisterMetaType<QList<radio_cap_packet>>() ;

    signal(SIGINT, handleCtrlC);

    haveRigCaps = false;

    setDefPrefs();

    getSettingsFilePath(settingsFile);

    loadSettings(); // Look for saved preferences

    setInitialTiming();

    openRig();

    rigConnections();

    setServerToPrefs();

    amTransmitting = false;

}

servermain::~servermain()
{
    rigThread->quit();
    rigThread->wait();
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
    }
    if (rigCtl != Q_NULLPTR) {
        delete rigCtl;
    }
    delete settings;

#if defined(PORTAUDIO)
    Pa_Terminate();
#endif

}

void servermain::openRig()
{
    // This function is intended to handle opening a connection to the rig.
    // the connection can be either serial or network,
    // and this function is also responsible for initiating the search for a rig model and capabilities.
    // Any errors, such as unable to open connection or unable to open port, are to be reported to the user.

    //TODO: if(hasRunPreviously)

    //TODO: if(useNetwork){...

    // } else {

    // if (prefs.fileWasNotFound) {
    //     showRigSettings(); // rig setting dialog box for network/serial, CIV, hostname, port, baud rate, serial device, etc
    // TODO: How do we know if the setting was loaded?


    // TODO: Use these if they are found
    if(!serialPortCL.isEmpty())
    {
        qDebug(logSystem()) << "Serial port specified by user: " << serialPortCL;
    } else {
        qDebug(logSystem()) << "Serial port not specified. ";
    }

    if(!hostCL.isEmpty())
    {
        qDebug(logSystem()) << "Remote host name specified by user: " << hostCL;
    }


    makeRig();

    if (prefs.enableLAN)
    {
        usingLAN = true;
        // We need to setup the tx/rx audio:
        emit sendCommSetup(prefs.radioCIVAddr, udpPrefs, rxSetup, txSetup, prefs.virtualSerialPort);
    } else {
        if( (prefs.serialPortRadio.toLower() == QString("auto")) && (serialPortCL.isEmpty()))
        {
            findSerialPort();

        } else {
            if(serialPortCL.isEmpty())
            {
                serialPortRig = prefs.serialPortRadio;
            } else {
                serialPortRig = serialPortCL;
            }
        }
        usingLAN = false;
        emit sendCommSetup(prefs.radioCIVAddr, serialPortRig, prefs.serialPortBaud,prefs.virtualSerialPort);
    }



}

void servermain::rigConnections()
{
    connect(this, SIGNAL(setCIVAddr(unsigned char)), rig, SLOT(setCIVAddr(unsigned char)));

    connect(this, SIGNAL(sendPowerOn()), rig, SLOT(powerOn()));
    connect(this, SIGNAL(sendPowerOff()), rig, SLOT(powerOff()));

    connect(rig, SIGNAL(havePTTStatus(bool)), this, SLOT(receivePTTstatus(bool)));
    connect(this, SIGNAL(setPTT(bool)), rig, SLOT(setPTT(bool)));
    connect(this, SIGNAL(getPTT()), rig, SLOT(getPTT()));
    connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));


    connect(this, SIGNAL(getDuplexMode()), rig, SLOT(getDuplexMode()));
    connect(this, SIGNAL(getTone()), rig, SLOT(getTone()));
    connect(this, SIGNAL(getTSQL()), rig, SLOT(getTSQL()));
    connect(this, SIGNAL(getRptAccessMode()), rig, SLOT(getRptAccessMode()));
 
    connect(this, SIGNAL(setScopeMode(spectrumMode)), rig, SLOT(setSpectrumMode(spectrumMode)));
    connect(this, SIGNAL(getScopeMode()), rig, SLOT(getScopeMode()));

    connect(this, SIGNAL(setFrequency(unsigned char, freqt)), rig, SLOT(setFrequency(unsigned char, freqt)));
    connect(this, SIGNAL(setScopeEdge(char)), rig, SLOT(setScopeEdge(char)));
    connect(this, SIGNAL(setScopeSpan(char)), rig, SLOT(setScopeSpan(char)));
    //connect(this, SIGNAL(getScopeMode()), rig, SLOT(getScopeMode()));
    connect(this, SIGNAL(getScopeEdge()), rig, SLOT(getScopeEdge()));
    connect(this, SIGNAL(getScopeSpan()), rig, SLOT(getScopeSpan()));
    connect(this, SIGNAL(setScopeFixedEdge(double,double,unsigned char)), rig, SLOT(setSpectrumBounds(double,double,unsigned char)));

    connect(this, SIGNAL(setMode(unsigned char, unsigned char)), rig, SLOT(setMode(unsigned char, unsigned char)));
    connect(this, SIGNAL(setMode(mode_info)), rig, SLOT(setMode(mode_info)));

    // Levels (read and write)
    // Levels: Query:
    connect(this, SIGNAL(getLevels()), rig, SLOT(getLevels()));
    connect(this, SIGNAL(getRfGain()), rig, SLOT(getRfGain()));
    connect(this, SIGNAL(getAfGain()), rig, SLOT(getAfGain()));
    connect(this, SIGNAL(getSql()), rig, SLOT(getSql()));
    connect(this, SIGNAL(getIfShift()), rig, SLOT(getIFShift()));
    connect(this, SIGNAL(getTPBFInner()), rig, SLOT(getTPBFInner()));
    connect(this, SIGNAL(getTPBFOuter()), rig, SLOT(getTPBFOuter()));
    connect(this, SIGNAL(getTxPower()), rig, SLOT(getTxLevel()));
    connect(this, SIGNAL(getMicGain()), rig, SLOT(getMicGain()));
    connect(this, SIGNAL(getSpectrumRefLevel()), rig, SLOT(getSpectrumRefLevel()));
    connect(this, SIGNAL(getModInputLevel(rigInput)), rig, SLOT(getModInputLevel(rigInput)));


    // Levels: Set:
    connect(this, SIGNAL(setRfGain(unsigned char)), rig, SLOT(setRfGain(unsigned char)));
    connect(this, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));
    connect(this, SIGNAL(setSql(unsigned char)), rig, SLOT(setSquelch(unsigned char)));
    connect(this, SIGNAL(setIFShift(unsigned char)), rig, SLOT(setIFShift(unsigned char)));
    connect(this, SIGNAL(setTPBFInner(unsigned char)), rig, SLOT(setTPBFInner(unsigned char)));
    connect(this, SIGNAL(setTPBFOuter(unsigned char)), rig, SLOT(setTPBFOuter(unsigned char)));
    connect(this, SIGNAL(setTxPower(unsigned char)), rig, SLOT(setTxPower(unsigned char)));
    connect(this, SIGNAL(setMicGain(unsigned char)), rig, SLOT(setMicGain(unsigned char)));
    connect(this, SIGNAL(setMonitorLevel(unsigned char)), rig, SLOT(setMonitorLevel(unsigned char)));
    connect(this, SIGNAL(setVoxGain(unsigned char)), rig, SLOT(setVoxGain(unsigned char)));
    connect(this, SIGNAL(setAntiVoxGain(unsigned char)), rig, SLOT(setAntiVoxGain(unsigned char)));
    connect(this, SIGNAL(setSpectrumRefLevel(int)), rig, SLOT(setSpectrumRefLevel(int)));
    connect(this, SIGNAL(setModLevel(rigInput, unsigned char)), rig, SLOT(setModInputLevel(rigInput, unsigned char)));

    // Rig and ATU info:
    connect(this, SIGNAL(startATU()), rig, SLOT(startATU()));
    connect(this, SIGNAL(setATU(bool)), rig, SLOT(setATU(bool)));
    connect(this, SIGNAL(getATUStatus()), rig, SLOT(getATUStatus()));
    connect(this, SIGNAL(getRigID()), rig, SLOT(getRigID()));
    connect(rig, SIGNAL(haveRigID(rigCapabilities)), this, SLOT(receiveRigID(rigCapabilities)));
    connect(this, SIGNAL(setAttenuator(unsigned char)), rig, SLOT(setAttenuator(unsigned char)));
    connect(this, SIGNAL(setPreamp(unsigned char)), rig, SLOT(setPreamp(unsigned char)));
    connect(this, SIGNAL(setAntenna(unsigned char, bool)), rig, SLOT(setAntenna(unsigned char, bool)));
    connect(this, SIGNAL(getPreamp()), rig, SLOT(getPreamp()));
    connect(this, SIGNAL(getAttenuator()), rig, SLOT(getAttenuator()));
    connect(this, SIGNAL(getAntenna()), rig, SLOT(getAntenna()));


    // Speech (emitted from rig speaker)
    connect(this, SIGNAL(sayAll()), rig, SLOT(sayAll()));
    connect(this, SIGNAL(sayFrequency()), rig, SLOT(sayFrequency()));
    connect(this, SIGNAL(sayMode()), rig, SLOT(sayMode()));

    // Date and Time:
    connect(this, SIGNAL(setTime(timekind)), rig, SLOT(setTime(timekind)));
    connect(this, SIGNAL(setDate(datekind)), rig, SLOT(setDate(datekind)));
    connect(this, SIGNAL(setUTCOffset(timekind)), rig, SLOT(setUTCOffset(timekind)));

}


void servermain::makeRig()
{
    if (rigThread == Q_NULLPTR)
    {
        rig = new rigCommander();
        rigThread = new QThread(this);

        // Thread:
        rig->moveToThread(rigThread);
        connect(rigThread, SIGNAL(started()), rig, SLOT(process()));
        connect(rigThread, SIGNAL(finished()), rig, SLOT(deleteLater()));
        rigThread->start();

        // Rig status and Errors:
        connect(rig, SIGNAL(haveSerialPortError(QString, QString)), this, SLOT(receiveSerialPortError(QString, QString)));
        connect(rig, SIGNAL(haveStatusUpdate(QString)), this, SLOT(receiveStatusUpdate(QString)));

        // Rig comm setup:
        connect(this, SIGNAL(sendCommSetup(unsigned char, udpPreferences, audioSetup, audioSetup, QString)), rig, SLOT(commSetup(unsigned char, udpPreferences, audioSetup, audioSetup, QString)));
        connect(this, SIGNAL(sendCommSetup(unsigned char, QString, quint32,QString)), rig, SLOT(commSetup(unsigned char, QString, quint32,QString)));
        connect(this, SIGNAL(setRTSforPTT(bool)), rig, SLOT(setRTSforPTT(bool)));

        connect(rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

        connect(this, SIGNAL(sendCloseComm()), rig, SLOT(closeComm()));
        connect(this, SIGNAL(sendChangeLatency(quint16)), rig, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(getRigCIV()), rig, SLOT(findRigs()));
        connect(this, SIGNAL(setRigID(unsigned char)), rig, SLOT(setRigID(unsigned char)));
        connect(rig, SIGNAL(discoveredRigID(rigCapabilities)), this, SLOT(receiveFoundRigID(rigCapabilities)));
        connect(rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

        connect(this, SIGNAL(requestRigState()), rig, SLOT(sendState()));
        connect(this, SIGNAL(stateUpdated()), rig, SLOT(stateUpdated()));
        connect(rig, SIGNAL(stateInfo(rigstate*)), this, SLOT(receiveStateInfo(rigstate*)));
        if (rigCtl != Q_NULLPTR) {
            connect(rig, SIGNAL(stateInfo(rigstate*)), rigCtl, SLOT(receiveStateInfo(rigstate*)));
            connect(rigCtl, SIGNAL(stateUpdated()), rig, SLOT(stateUpdated()));
        }
    }
}

void servermain::removeRig()
{
    if (rigThread != Q_NULLPTR)
    {
        if (rigCtl != Q_NULLPTR) {
            rigCtl->disconnect();
        }
        rigThread->disconnect();

        rig->disconnect();

        delete rigThread;
        delete rig;
        rig = Q_NULLPTR;
    }

}


void servermain::findSerialPort()
{
    // Find the ICOM radio connected, or, if none, fall back to OS default.
    // qInfo(logSystem()) << "Searching for serial port...";
    QDirIterator it73("/dev/serial/by-id", QStringList() << "*IC-7300*", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator it97("/dev/serial", QStringList() << "*IC-9700*A*", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator it785x("/dev/serial", QStringList() << "*IC-785*A*", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator it705("/dev/serial", QStringList() << "*IC-705*A", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator it7610("/dev/serial", QStringList() << "*IC-7610*A", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator itR8600("/dev/serial", QStringList() << "*IC-R8600*A", QDir::Files, QDirIterator::Subdirectories);

    if(!it73.filePath().isEmpty())
    {
        // IC-7300
        serialPortRig = it73.filePath(); // first
    } else if(!it97.filePath().isEmpty())
    {
        // IC-9700
        serialPortRig = it97.filePath();
    } else if(!it785x.filePath().isEmpty())
    {
        // IC-785x
        serialPortRig = it785x.filePath();
    } else if(!it705.filePath().isEmpty())
    {
        // IC-705
        serialPortRig = it705.filePath();
    } else if(!it7610.filePath().isEmpty())
    {
        // IC-7610
        serialPortRig = it7610.filePath();
    } else if(!itR8600.filePath().isEmpty())
    {
        // IC-R8600
        serialPortRig = itR8600.filePath();
    } else {
        //fall back:
        qInfo(logSystem()) << "Could not find Icom serial port. Falling back to OS default. Use --port to specify, or modify preferences.";
#ifdef Q_OS_MAC
        serialPortRig = QString("/dev/tty.SLAB_USBtoUART");
#endif
#ifdef Q_OS_LINUX
        serialPortRig = QString("/dev/ttyUSB0");
#endif
#ifdef Q_OS_WIN
        serialPortRig = QString("COM1");
#endif
    }
}

void servermain::receiveStatusUpdate(QString text)
{
    if (text != lastMessage) {
        std::cout << text.toLocal8Bit().toStdString() << "\n";
        lastMessage = text;
    }
}


void servermain::receiveCommReady()
{
    qInfo(logSystem()) << "Received CommReady!! ";
    if(!usingLAN)
    {
        // usingLAN gets set when we emit the sendCommSetup signal.
        // If we're not using the LAN, then we're on serial, and
        // we already know the baud rate and can calculate the timing parameters.
        calculateTimingParameters();
    }
    if(prefs.radioCIVAddr == 0)
    {
        // tell rigCommander to broadcast a request for all rig IDs.
        // qInfo(logSystem()) << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
        emit getRigCIV();
        issueDelayedCommand(cmdGetRigCIV);
        delayedCommand->start();
    } else {
        // don't bother, they told us the CIV they want, stick with it.
        // We still query the rigID to find the model, but at least we know the CIV.
        qInfo(logSystem()) << "Skipping automatic CIV, using user-supplied value of " << prefs.radioCIVAddr;
        if(prefs.CIVisRadioModel)
        {
            qInfo(logSystem()) << "Skipping Rig ID query, using user-supplied model from CI-V address: " << prefs.radioCIVAddr;
            emit setRigID(prefs.radioCIVAddr);
        } else {
            emit getRigID();
            getInitialRigState();
        }
    }
}


void servermain::receiveFoundRigID(rigCapabilities rigCaps)
{
    // Entry point for unknown rig being identified at the start of the program.
    //now we know what the rig ID is:
    //qInfo(logSystem()) << "In wfview, we now have a reply to our request for rig identity sent to CIV BROADCAST.";

    if(rig->usingLAN())
    {
        usingLAN = true;
    } else {
        usingLAN = false;
    }

    receiveRigID(rigCaps);
    getInitialRigState();

    return;
}

void servermain::receiveSerialPortError(QString port, QString errorText)
{
    qInfo(logSystem()) << "servermain: received serial port error for port: " << port << " with message: " << errorText;

    // TODO: Dialog box, exit, etc
}


void servermain::getSettingsFilePath(QString settingsFile)
{
    if (settingsFile.isNull()) {
        settings = new QSettings();
    }
    else
    {
        QString file = settingsFile;
        QFile info(settingsFile);
        QString path="";
        if (!QFileInfo(info).isAbsolute())
        {
            path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (path.isEmpty())
            {
                path = QDir::homePath();
            }
            path = path + "/";
            file = info.fileName();
        }

        qInfo(logSystem()) << "Loading settings from:" << path + file;
        settings = new QSettings(path + file, QSettings::Format::IniFormat);
    }
}


void servermain::setInitialTiming()
{
    loopTickCounter = 0;
    delayedCmdIntervalLAN_ms = 70; // interval for regular delayed commands, including initial rig/UI state queries
    delayedCmdIntervalSerial_ms = 100; // interval for regular delayed commands, including initial rig/UI state queries
    delayedCmdStartupInterval_ms = 250; // interval for rigID polling
    delayedCommand = new QTimer(this);
    delayedCommand->setInterval(delayedCmdStartupInterval_ms); // 250ms until we find rig civ and id, then 100ms.
    delayedCommand->setSingleShot(false);
    connect(delayedCommand, SIGNAL(timeout()), this, SLOT(sendRadioCommandLoop()));

    // TODO: Remove this:
//    periodicPollingTimer = new QTimer(this);
//    periodicPollingTimer->setInterval(10);
//    periodicPollingTimer->setSingleShot(false);
    //connect(periodicPollingTimer, SIGNAL(timeout()), this, SLOT(sendRadioCommandLoop()));

    pttTimer = new QTimer(this);
    pttTimer->setInterval(180*1000); // 3 minute max transmit time in ms
    pttTimer->setSingleShot(true);
    connect(pttTimer, SIGNAL(timeout()), this, SLOT(handlePttLimit()));

}

void servermain::setServerToPrefs()
{
	
    // Start server if enabled in config
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
        serverThread = Q_NULLPTR;
        udp = Q_NULLPTR;
    }

    if (serverConfig.enabled) {
        serverConfig.lan = prefs.enableLAN;
        qInfo(logAudio()) << "Audio Input device " << serverRxSetup.name;
        qInfo(logAudio()) << "Audio Output device " << serverTxSetup.name;
        udp = new udpServer(serverConfig, serverTxSetup, serverRxSetup);

        serverThread = new QThread(this);

        udp->moveToThread(serverThread);


        connect(this, SIGNAL(initServer()), udp, SLOT(init()));
        connect(serverThread, SIGNAL(finished()), udp, SLOT(deleteLater()));

        if (rig != Q_NULLPTR) {
            connect(rig, SIGNAL(haveAudioData(audioPacket)), udp, SLOT(receiveAudioData(audioPacket)));
            connect(rig, SIGNAL(haveDataForServer(QByteArray)), udp, SLOT(dataForServer(QByteArray)));
            connect(udp, SIGNAL(haveDataFromServer(QByteArray)), rig, SLOT(dataFromServer(QByteArray)));
        }

        if (!prefs.enableLAN) {
            connect(udp, SIGNAL(haveNetworkStatus(QString)), this, SLOT(receiveStatusUpdate(QString)));
        }

        serverThread->start();

        emit initServer();

        connect(this, SIGNAL(sendRigCaps(rigCapabilities)), udp, SLOT(receiveRigCaps(rigCapabilities)));

    }
}


void servermain::setDefPrefs()
{
    defPrefs.useFullScreen = false;
    defPrefs.useDarkMode = true;
    defPrefs.useSystemTheme = false;
    defPrefs.drawPeaks = true;
    defPrefs.wfAntiAlias = false;
    defPrefs.wfInterpolate = true;
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.CIVisRadioModel = false;
    defPrefs.forceRTSasPTT = false;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.enablePTT = false;
    defPrefs.niceTS = true;
    defPrefs.enableRigCtlD = false;
    defPrefs.rigCtlPort = 4533;
    defPrefs.virtualSerialPort = QString("none");
    defPrefs.localAFgain = 255;
    defPrefs.wflength = 160;
    defPrefs.confirmExit = true;
    defPrefs.confirmPowerOff = true;
    defPrefs.meter2Type = meterNone;

    udpDefPrefs.ipAddress = QString("");
    udpDefPrefs.controlLANPort = 50001;
    udpDefPrefs.serialLANPort = 50002;
    udpDefPrefs.audioLANPort = 50003;
    udpDefPrefs.username = QString("");
    udpDefPrefs.password = QString("");
    udpDefPrefs.clientName = QHostInfo::localHostName();
}

void servermain::loadSettings()
{
    qInfo(logSystem()) << "Loading settings from " << settings->fileName();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    prefs.radioCIVAddr = (unsigned char)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
    prefs.CIVisRadioModel = (bool)settings->value("CIVisRadioModel", defPrefs.CIVisRadioModel).toBool();
    prefs.forceRTSasPTT = (bool)settings->value("ForceRTSasPTT", defPrefs.forceRTSasPTT).toBool();
    prefs.serialPortRadio = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
    prefs.serialPortBaud = (quint32)settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();

    if (prefs.serialPortBaud > 0)
    {
        serverConfig.baudRate = prefs.serialPortBaud;
    }

    prefs.virtualSerialPort = settings->value("VirtualSerialPort", defPrefs.virtualSerialPort).toString();

    prefs.localAFgain = (unsigned char)settings->value("localAFgain", defPrefs.localAFgain).toUInt();
    rxSetup.localAFgain = prefs.localAFgain;
    txSetup.localAFgain = 255;

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs.enablePTT = settings->value("EnablePTT", defPrefs.enablePTT).toBool();
    prefs.niceTS = settings->value("NiceTS", defPrefs.niceTS).toBool();
    settings->endGroup();

    settings->beginGroup("LAN");

    prefs.enableLAN = settings->value("EnableLAN", defPrefs.enableLAN).toBool();

    prefs.enableRigCtlD = settings->value("EnableRigCtlD", defPrefs.enableRigCtlD).toBool();
    prefs.rigCtlPort = settings->value("RigCtlPort", defPrefs.rigCtlPort).toInt();
    udpPrefs.ipAddress = settings->value("IPAddress", udpDefPrefs.ipAddress).toString();
    udpPrefs.controlLANPort = settings->value("ControlLANPort", udpDefPrefs.controlLANPort).toInt();
    udpPrefs.username = settings->value("Username", udpDefPrefs.username).toString();
    udpPrefs.password = settings->value("Password", udpDefPrefs.password).toString();

    rxSetup.isinput = false;
    txSetup.isinput = true;

    rxSetup.latency = settings->value("AudioRXLatency", "150").toInt();
    txSetup.latency = settings->value("AudioTXLatency", "150").toInt();
    rxSetup.samplerate = settings->value("AudioRXSampleRate", "48000").toInt();
    txSetup.samplerate = rxSetup.samplerate;
    rxSetup.codec = settings->value("AudioRXCodec", "4").toInt();
    txSetup.codec = settings->value("AudioTXCodec", "4").toInt();
    rxSetup.name = settings->value("AudioOutput", "").toString();
    qInfo(logGui()) << "Got Audio Output: " << rxSetup.name;

    txSetup.name = settings->value("AudioInput", "").toString();
   // qInfo(logGui()) << "Got Audio Input: " << txSetup.name;    int audioInputIndex = ui->audioInputCombo->findText(txSetup.name);

    rxSetup.resampleQuality = settings->value("ResampleQuality", "4").toInt();
    txSetup.resampleQuality = rxSetup.resampleQuality;

    udpPrefs.clientName = settings->value("ClientName", udpDefPrefs.clientName).toString();

    settings->endGroup();


    settings->beginGroup("Server");
    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.controlPort = settings->value("ServerControlPort", 50001).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", 50002).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", 50003).toInt();
    int numUsers = settings->value("ServerNumUsers", 2).toInt();
    serverConfig.users.clear();
    for (int f = 0; f < numUsers; f++)
    {
        SERVERUSER user;
        user.username = settings->value("ServerUsername_" + QString::number(f), "").toString();
        user.password = settings->value("ServerPassword_" + QString::number(f), "").toString();
        user.userType = settings->value("ServerUserType_" + QString::number(f), 0).toInt();
        serverConfig.users.append(user);
    }

    serverRxSetup.isinput = true;
    serverTxSetup.isinput = false;
    serverRxSetup.localAFgain = 255;
    serverTxSetup.localAFgain = 255;

    serverRxSetup.name = settings->value("ServerAudioInput", "").toString();
    qInfo(logGui()) << "Got Server Audio Input: " << serverRxSetup.name;

    serverRxSetup.resampleQuality = rxSetup.resampleQuality;
    serverTxSetup.resampleQuality = serverRxSetup.resampleQuality;

    serverTxSetup.name = settings->value("ServerAudioOutput", "").toString();
    qInfo(logGui()) << "Got Server Audio Output: " << serverTxSetup.name;

    settings->endGroup();

    // Memory channels

    settings->beginGroup("Memory");
    int size = settings->beginReadArray("Channel");
    int chan = 0;
    double freq;
    unsigned char mode;
    bool isSet;

    // Annoying: QSettings will write the array to the
    // preference file starting the array at 1 and ending at 100.
    // Thus, we are writing the channel number each time.
    // It is also annoying that they get written with their array
    // numbers in alphabetical order without zero padding.
    // Also annoying that the preference groups are not written in
    // the order they are specified here.

    for (int i = 0; i < size; i++)
    {
        settings->setArrayIndex(i);
        chan = settings->value("chan", 0).toInt();
        freq = settings->value("freq", 12.345).toDouble();
        mode = settings->value("mode", 0).toInt();
        isSet = settings->value("isSet", false).toBool();

        if (isSet)
        {
            mem.setPreset(chan, freq, (mode_kind)mode);
        }
    }

    settings->endArray();
    settings->endGroup();





#if defined(RTAUDIO)

#if defined(Q_OS_LINUX)
    RtAudio* audio = new RtAudio(RtAudio::Api::LINUX_ALSA);
#elif defined(Q_OS_WIN)
    RtAudio* audio = new RtAudio(RtAudio::Api::WINDOWS_WASAPI);
#elif defined(Q_OS_MACX)
    RtAudio* audio = new RtAudio(RtAudio::Api::MACOSX_CORE);
#endif

    // Enumerate audio devices, need to do before settings are loaded.
    std::map<int, std::string> apiMap;
    apiMap[RtAudio::MACOSX_CORE] = "OS-X Core Audio";
    apiMap[RtAudio::WINDOWS_ASIO] = "Windows ASIO";
    apiMap[RtAudio::WINDOWS_DS] = "Windows DirectSound";
    apiMap[RtAudio::WINDOWS_WASAPI] = "Windows WASAPI";
    apiMap[RtAudio::UNIX_JACK] = "Jack Client";
    apiMap[RtAudio::LINUX_ALSA] = "Linux ALSA";
    apiMap[RtAudio::LINUX_PULSE] = "Linux PulseAudio";
    apiMap[RtAudio::LINUX_OSS] = "Linux OSS";
    apiMap[RtAudio::RTAUDIO_DUMMY] = "RtAudio Dummy";

    std::vector< RtAudio::Api > apis;
    RtAudio::getCompiledApi(apis);

    qInfo(logAudio()) << "RtAudio Version " << QString::fromStdString(RtAudio::getVersion());

    qInfo(logAudio()) << "Compiled APIs:";
    for (unsigned int i = 0; i < apis.size(); i++) {
        qInfo(logAudio()) << "  " << QString::fromStdString(apiMap[apis[i]]);
    }

    RtAudio::DeviceInfo info;

    qInfo(logAudio()) << "Current API: " << QString::fromStdString(apiMap[audio->getCurrentApi()]);

    unsigned int devices = audio->getDeviceCount();
    qInfo(logAudio()) << "Found " << devices << " audio device(s) *=default";

    for (unsigned int i = 1; i < devices; i++) {
        info = audio->getDeviceInfo(i);
        if (info.outputChannels > 0) {
            if (serverTxSetup.name == info->name) {
                serverTxSetup.port = i;
            }
        }
        if (info.inputChannels > 0) {
            if (serverRxSetup.name == info->name) {
                serverRxSetup.port = i;
            }
        }
    }

    delete audio;

#elif defined(PORTAUDIO)
    // Use PortAudio device enumeration

    PaError err;

    err = Pa_Initialize();

    if (err != paNoError)
    {
        qInfo(logAudio()) << "ERROR: Cannot initialize Portaudio";
    }

    qInfo(logAudio()) << "PortAudio version: " << Pa_GetVersionInfo()->versionText;

    int numDevices;
    numDevices = Pa_GetDeviceCount();
    qInfo(logAudio()) << "Pa_CountDevices returned" << numDevices;

    const   PaDeviceInfo* info;
    for (int i = 0; i < numDevices; i++)
    {
        info = Pa_GetDeviceInfo(i);
        if (info->maxInputChannels > 0) {
            if (serverTxSetup.name == info->name) {
                serverTxSetup.port = i;
            }
        }
        if (info->maxOutputChannels > 0) {
            if (serverRxSetup.name == info->name) {
                serverRxSetup.port = i;
            }
        }
    }
#else

    // If no external library is configured, use QTMultimedia
    // 
    // Set these to default audio devices initially.
    rxSetup.port = QAudioDeviceInfo::defaultOutputDevice();
    txSetup.port = QAudioDeviceInfo::defaultInputDevice();
    serverRxSetup.port = QAudioDeviceInfo::defaultInputDevice();
    serverTxSetup.port = QAudioDeviceInfo::defaultOutputDevice();


    // Enumerate audio devices, need to do before settings are loaded.
    const auto audioOutputs = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const QAudioDeviceInfo& deviceInfo : audioOutputs) {
        if (deviceInfo.deviceName() == serverTxSetup.name) {
            serverTxSetup.port = deviceInfo;
            qInfo(logGui()) << "Setting Server Audio Input: " << serverTxSetup.name;
        }
    }

    const auto audioInputs = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for (const QAudioDeviceInfo& deviceInfo : audioInputs) {
        if (deviceInfo.deviceName() == serverRxSetup.name) {
            serverRxSetup.port = deviceInfo;
            qInfo(logGui()) << "Setting Server Audio Output: " << serverRxSetup.name;
        }
    }

#endif

}



void servermain:: getInitialRigState()
{
    // Initial list of queries to the radio.
    // These are made when the program starts up
    // and are used to adjust the UI to match the radio settings
    // the polling interval is set at 200ms. Faster is possible but slower
    // computers will glitch occassionally.

    issueDelayedCommand(cmdGetFreq);
    issueDelayedCommand(cmdGetMode);

    issueDelayedCommand(cmdNone);

    issueDelayedCommand(cmdGetFreq);
    issueDelayedCommand(cmdGetMode);

    // From left to right in the UI:
    if (rigCaps.hasTransmit) 
    {
        issueDelayedCommand(cmdGetDataMode);
        issueDelayedCommand(cmdGetModInput);
        issueDelayedCommand(cmdGetModDataInput);
    }
    issueDelayedCommand(cmdGetRxGain);
    issueDelayedCommand(cmdGetAfGain);
    issueDelayedCommand(cmdGetSql);
    
    if (rigCaps.hasTransmit) 
    {
        issueDelayedCommand(cmdGetTxPower);
        issueDelayedCommand(cmdGetCurrentModLevel); // level for currently selected mod sources
    }
    
    issueDelayedCommand(cmdGetSpectrumRefLevel);
    issueDelayedCommand(cmdGetDuplexMode);

    if(rigCaps.hasSpectrum)
    {
        issueDelayedCommand(cmdDispEnable);
        issueDelayedCommand(cmdSpecOn);
    }

    if (rigCaps.hasTransmit)
    {
        issueDelayedCommand(cmdGetModInput);
        issueDelayedCommand(cmdGetModDataInput);
    }

    if(rigCaps.hasCTCSS)
    {
        issueDelayedCommand(cmdGetTone);
        issueDelayedCommand(cmdGetTSQL);
    }
    if(rigCaps.hasDTCS)
    {
        issueDelayedCommand(cmdGetDTCS);
    }
    issueDelayedCommand(cmdGetRptAccessMode);

    if(rigCaps.hasAntennaSel)
    {
        issueDelayedCommand(cmdGetAntenna);
    }
    if(rigCaps.hasAttenuator)
    {
        issueDelayedCommand(cmdGetAttenuator);
    }
    if(rigCaps.hasPreamp)
    {
        issueDelayedCommand(cmdGetPreamp);
    }

    issueDelayedCommand(cmdGetRitEnabled);
    issueDelayedCommand(cmdGetRitValue);

    if(rigCaps.hasIFShift)
        issueDelayedCommand(cmdGetIFShift);
    if(rigCaps.hasTBPF)
    {
        issueDelayedCommand(cmdGetTPBFInner);
        issueDelayedCommand(cmdGetTPBFOuter);
    }

    if(rigCaps.hasSpectrum)
    {
        issueDelayedCommand(cmdGetSpectrumMode);
        issueDelayedCommand(cmdGetSpectrumSpan);
    }

    issueDelayedCommand(cmdNone);
    issueDelayedCommand(cmdStartRegularPolling);

    if(rigCaps.hasATU)
    {
        issueDelayedCommand(cmdGetATUStatus);
    }

    delayedCommand->start();
}

void servermain::receivePTTstatus(bool pttOn)
{
    // This is the only place where amTransmitting and the transmit button text should be changed:
    //qInfo(logSystem()) << "PTT status: " << pttOn;
    amTransmitting = pttOn;
}


void servermain::doCmd(commandtype cmddata)
{
    cmds cmd = cmddata.cmd;
    std::shared_ptr<void> data = cmddata.data;

    // This switch is for commands with parameters.
    // the "default" for non-parameter commands is to call doCmd(cmd).
    switch (cmd)
    {
        case cmdSetFreq:
        {
            lastFreqCmdTime_ms = QDateTime::currentMSecsSinceEpoch();
            freqt f = (*std::static_pointer_cast<freqt>(data));
            emit setFrequency(0,f);
            break;
        }
        case cmdSetMode:
        {
            mode_info m = (*std::static_pointer_cast<mode_info>(data));
            emit setMode(m);
            break;
        }
        case cmdSetTxPower:
        {
            unsigned char txpower = (*std::static_pointer_cast<unsigned char>(data));
            emit setTxPower(txpower);
            break;
        }
        case cmdSetMicGain:
        {
            unsigned char micgain = (*std::static_pointer_cast<unsigned char>(data));
            emit setTxPower(micgain);
            break;
        }
        case cmdSetRxRfGain:
        {
            unsigned char rfgain = (*std::static_pointer_cast<unsigned char>(data));
            emit setRfGain(rfgain);
            break;
        }
        case cmdSetModLevel:
        {
            unsigned char modlevel = (*std::static_pointer_cast<unsigned char>(data));
            rigInput currentIn;
            if(usingDataMode)
            {
                currentIn = currentModDataSrc;
            } else {
                currentIn = currentModSrc;
            }
            emit setModLevel(currentIn, modlevel);
            break;
        }
        case cmdSetAfGain:
        {
            unsigned char afgain = (*std::static_pointer_cast<unsigned char>(data));
            emit setAfGain(afgain);
            break;
        }
        case cmdSetSql:
        {
            unsigned char sqlLevel = (*std::static_pointer_cast<unsigned char>(data));
            emit setSql(sqlLevel);
            break;
        }
        case cmdSetIFShift:
        {
            unsigned char IFShiftLevel = (*std::static_pointer_cast<unsigned char>(data));
            emit setIFShift(IFShiftLevel);
            break;
        }
        case cmdSetTPBFInner:
        {
            unsigned char innterLevel = (*std::static_pointer_cast<unsigned char>(data));
            emit setTPBFInner(innterLevel);
            break;
        }
        case cmdSetTPBFOuter:
        {
            unsigned char outerLevel = (*std::static_pointer_cast<unsigned char>(data));
            emit setTPBFOuter(outerLevel);
            break;
        }
        case cmdSetPTT:
        {
            bool pttrequest = (*std::static_pointer_cast<bool>(data));
            emit setPTT(pttrequest);
            break;
        }
        case cmdSetATU:
        {
            bool atuOn = (*std::static_pointer_cast<bool>(data));
            emit setATU(atuOn);
            break;
        }
        case cmdSetUTCOffset:
        {
            timekind u = (*std::static_pointer_cast<timekind>(data));
            emit setUTCOffset(u);
            break;
        }
        case cmdSetTime:
        {
            timekind t = (*std::static_pointer_cast<timekind>(data));
            emit setTime(t);
            break;
        }
        case cmdSetDate:
        {
            datekind d = (*std::static_pointer_cast<datekind>(data));
            emit setDate(d);
            break;
        }
        default:
            doCmd(cmd);
            break;
    }

}


void servermain::doCmd(cmds cmd)
{
    // Use this function to take action upon a command.
    switch (cmd)
    {
    case cmdNone:
        //qInfo(logSystem()) << "NOOP";
        break;
    case cmdGetRigID:
        emit getRigID();
        break;
    case cmdGetRigCIV:
        // if(!know rig civ already)
        if (!haveRigCaps)
        {
            emit getRigCIV();
            issueDelayedCommand(cmdGetRigCIV); // This way, we stay here until we get an answer.
        }
        break;
    case cmdGetFreq:
        emit getFrequency();
        break;
    case cmdGetMode:
        emit getMode();
        break;
    case cmdGetDataMode:
        if (rigCaps.hasDataModes)
            emit getDataMode();
        break;
    case cmdSetModeFilter:
        emit setMode(setModeVal, setFilterVal);
        break;
    case cmdSetDataModeOff:
        break;
    case cmdSetDataModeOn:
        break;
    case cmdGetRitEnabled:
        emit getRitEnabled();
        break;
    case cmdGetRitValue:
        emit getRitValue();
        break;
    case cmdGetModInput:
        emit getModInput(false);
        break;
    case cmdGetModDataInput:
        emit getModInput(true);
        break;
    case cmdGetCurrentModLevel:
        // TODO: Add delay between these queries
        emit getModInputLevel(currentModSrc);
        emit getModInputLevel(currentModDataSrc);
        break;
    case cmdGetDuplexMode:
        emit getDuplexMode();
        break;
    case cmdGetTone:
        emit getTone();
        break;
    case cmdGetTSQL:
        emit getTSQL();
        break;
    case cmdGetDTCS:
        emit getDTCS();
        break;
    case cmdGetRptAccessMode:
        emit getRptAccessMode();
        break;
    case cmdDispEnable:
        emit scopeDisplayEnable();
        break;
    case cmdDispDisable:
        emit scopeDisplayDisable();
        break;
    case cmdGetSpectrumMode:
        emit getScopeMode();
        break;
    case cmdGetSpectrumSpan:
        emit getScopeSpan();
        break;
    case cmdSpecOn:
        emit spectOutputEnable();
        break;
    case cmdSpecOff:
        emit spectOutputDisable();
        break;
    case cmdGetRxGain:
        emit getRfGain();
        break;
    case cmdGetAfGain:
        emit getAfGain();
        break;
    case cmdGetSql:
        emit getSql();
        break;
    case cmdGetIFShift:
        emit getIfShift();
        break;
    case cmdGetTPBFInner:
        emit getTPBFInner();
        break;
    case cmdGetTPBFOuter:
        emit getTPBFOuter();
        break;
    case cmdGetTxPower:
        emit getTxPower();
        break;
    case cmdGetMicGain:
        emit getMicGain();
        break;
    case cmdGetSpectrumRefLevel:
        emit getSpectrumRefLevel();
        break;
    case cmdGetATUStatus:
        if (rigCaps.hasATU)
            emit getATUStatus();
        break;
    case cmdStartATU:
        if (rigCaps.hasATU)
            emit startATU();
        break;
    case cmdGetAttenuator:
        emit getAttenuator();
        break;
    case cmdGetPreamp:
        emit getPreamp();
        break;
    case cmdGetAntenna:
        emit getAntenna();
        break;
    case cmdScopeCenterMode:
        emit setScopeMode(spectModeCenter);
        break;
    case cmdScopeFixedMode:
        emit setScopeMode(spectModeFixed);
        break;
    case cmdGetPTT:
        emit getPTT();
        break;
    case cmdGetTxRxMeter:
        if (amTransmitting)
            emit getMeters(meterPower);
        else
            emit getMeters(meterS);
        break;
    case cmdGetSMeter:
        if (!amTransmitting)
            emit getMeters(meterS);
        break;
    case cmdGetCenterMeter:
        if (!amTransmitting)
            emit getMeters(meterCenter);
        break;
    case cmdGetPowerMeter:
        if (amTransmitting)
            emit getMeters(meterPower);
        break;
    case cmdGetSWRMeter:
        if (amTransmitting)
            emit getMeters(meterSWR);
        break;
    case cmdGetIdMeter:
        emit getMeters(meterCurrent);
        break;
    case cmdGetVdMeter:
        emit getMeters(meterVoltage);
        break;
    case cmdGetALCMeter:
        if (amTransmitting)
            emit getMeters(meterALC);
        break;
    case cmdGetCompMeter:
        if (amTransmitting)
            emit getMeters(meterComp);
        break;
    case cmdStartRegularPolling:
        runPeriodicCommands = true;
        break;
    case cmdStopRegularPolling:
        runPeriodicCommands = false;
        break;
    case cmdQueNormalSpeed:
        if (usingLAN)
        {
            delayedCommand->setInterval(delayedCmdIntervalLAN_ms);
        }
        else {
            delayedCommand->setInterval(delayedCmdIntervalSerial_ms);
        }
        break;
    default:
        qInfo(logSystem()) << __PRETTY_FUNCTION__ << "WARNING: Command fell through of type: " << (unsigned int)cmd;
        break;
    }
}




void servermain::sendRadioCommandLoop()
{
    // Called by the periodicPollingTimer, see setInitialTiming()

    if(!(loopTickCounter % 2))
    {
        // if ther's a command waiting, run it.
        if(!delayedCmdQue.empty())
        {
            commandtype cmddata = delayedCmdQue.front();
            delayedCmdQue.pop_front();
            doCmd(cmddata);
        } else if(!(loopTickCounter % 10))
        {
            // pick from useful queries to make now and then
            if(haveRigCaps && !slowPollCmdQueue.empty())
            {

                int nCmds = slowPollCmdQueue.size();
                cmds sCmd = slowPollCmdQueue[(slowCmdNum++)%nCmds];
                doCmd(sCmd);
            }
        }
    } else {
        // odd-number ticks:
        // s-meter or other metering
        if(haveRigCaps && !periodicCmdQueue.empty())
        {
            int nCmds = periodicCmdQueue.size();
            cmds pcmd = periodicCmdQueue[ (pCmdNum++)%nCmds ];
            doCmd(pcmd);
        }
    }
    loopTickCounter++;
}

void servermain::issueDelayedCommand(cmds cmd)
{
    // Append to end of command queue
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = NULL;
    delayedCmdQue.push_back(cmddata);
}

void servermain::issueDelayedCommandPriority(cmds cmd)
{
    // Places the new command at the top of the queue
    // Use only when needed.
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = NULL;
    delayedCmdQue.push_front(cmddata);
}

void servermain::issueDelayedCommandUnique(cmds cmd)
{
    // Use this function to insert commands where
    // multiple (redundant) commands don't make sense.

    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = NULL;

    // The following is both expensive and not that great,
    // since it does not check if the arguments are the same.
    bool found = false;
    for(unsigned int i=0; i < delayedCmdQue.size(); i++)
    {
        if(delayedCmdQue.at(i).cmd == cmd)
        {
            found = true;
            break;
        }
    }

    if(!found)
    {
        delayedCmdQue.push_front(cmddata);
    }

//    if( std::find(delayedCmdQue.begin(), delayedCmdQue.end(), cmddata ) == delayedCmdQue.end())
//    {
//        delayedCmdQue.push_front(cmddata);
//    }

}

void servermain::issueCmd(cmds cmd, mode_info m)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<mode_info>(new mode_info(m));
    delayedCmdQue.push_back(cmddata);
}

void servermain::issueCmd(cmds cmd, freqt f)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<freqt>(new freqt(f));
    delayedCmdQue.push_back(cmddata);
}

void servermain::issueCmd(cmds cmd, timekind t)
{
    qDebug(logSystem()) << "Issuing timekind command with data: " << t.hours << " hours, " << t.minutes << " minutes, " << t.isMinus << " isMinus";
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<timekind>(new timekind(t));
    delayedCmdQue.push_front(cmddata);
}

void servermain::issueCmd(cmds cmd, datekind d)
{
    qDebug(logSystem()) << "Issuing datekind command with data: " << d.day << " day, " << d.month << " month, " << d.year << " year.";
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<datekind>(new datekind(d));
    delayedCmdQue.push_front(cmddata);
}

void servermain::issueCmd(cmds cmd, int i)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<int>(new int(i));
    delayedCmdQue.push_back(cmddata);
}

void servermain::issueCmd(cmds cmd, char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<char>(new char(c));
    delayedCmdQue.push_back(cmddata);
}

void servermain::issueCmd(cmds cmd, bool b)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<bool>(new bool(b));
    delayedCmdQue.push_back(cmddata);
}

void servermain::issueCmd(cmds cmd, unsigned char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<unsigned char>(new unsigned char(c));
    delayedCmdQue.push_back(cmddata);
}

void servermain::issueCmdUniquePriority(cmds cmd, bool b)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<bool>(new bool(b));
    delayedCmdQue.push_front(cmddata);
    removeSimilarCommand(cmd);
}

void servermain::issueCmdUniquePriority(cmds cmd, unsigned char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<unsigned char>(new unsigned char(c));
    delayedCmdQue.push_front(cmddata);
    removeSimilarCommand(cmd);
}

void servermain::issueCmdUniquePriority(cmds cmd, char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<char>(new char(c));
    delayedCmdQue.push_front(cmddata);
    removeSimilarCommand(cmd);
}

void servermain::issueCmdUniquePriority(cmds cmd, freqt f)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<freqt>(new freqt(f));
    delayedCmdQue.push_front(cmddata);
    removeSimilarCommand(cmd);
}

void servermain::removeSimilarCommand(cmds cmd)
{
    // pop anything out that is of the same kind of command:
    // pop anything out that is of the same kind of command:
    // Start at 1 since we put one in at zero that we want to keep.
    for(unsigned int i=1; i < delayedCmdQue.size(); i++)
    {
        if(delayedCmdQue.at(i).cmd == cmd)
        {
            //delayedCmdQue[i].cmd = cmdNone;
            delayedCmdQue.erase(delayedCmdQue.begin()+i);
            // i -= 1;
        }
    }
}

void servermain::receiveRigID(rigCapabilities rigCaps)
{
    // Note: We intentionally request rigID several times
    // because without rigID, we can't do anything with the waterfall.
    if(haveRigCaps)
    {
        return;
    } else {

        qDebug(logSystem()) << "Rig name: " << rigCaps.modelName;
        qDebug(logSystem()) << "Has LAN capabilities: " << rigCaps.hasLan;
        qDebug(logSystem()) << "Rig ID received into servermain: spectLenMax: " << rigCaps.spectLenMax;
        qDebug(logSystem()) << "Rig ID received into servermain: spectAmpMax: " << rigCaps.spectAmpMax;
        qDebug(logSystem()) << "Rig ID received into servermain: spectSeqMax: " << rigCaps.spectSeqMax;
        qDebug(logSystem()) << "Rig ID received into servermain: hasSpectrum: " << rigCaps.hasSpectrum;

        this->rigCaps = rigCaps;
        this->spectWidth = rigCaps.spectLenMax; // used once haveRigCaps is true.
        haveRigCaps = true;
        // Added so that server receives rig capabilities.
        emit sendRigCaps(rigCaps);

    }
}

void servermain::initPeriodicCommands()
{
    // This function places periodic polling commands into a queue.
    // The commands are run using a timer,
    // and the timer is started by the delayed command cmdStartPeriodicTimer.

}

void servermain::insertPeriodicCommand(cmds cmd, unsigned char priority)
{
    // TODO: meaningful priority
    // These commands get run at the fastest pace possible
    // Typically just metering.
    if(priority < 10)
    {
        periodicCmdQueue.push_front(cmd);
    } else {
        periodicCmdQueue.push_back(cmd);
    }
}

void servermain::insertPeriodicCommandUnique(cmds cmd)
{
    // Use this function to insert a non-duplicate command
    // into the fast periodic polling queue, typically
    // meter commands where high refresh rates are desirable.

    removePeriodicCommand(cmd);
    periodicCmdQueue.push_front(cmd);
}

void servermain::removePeriodicCommand(cmds cmd)
{
    while(true)
    {
        auto it = std::find(this->periodicCmdQueue.begin(), this->periodicCmdQueue.end(), cmd);
        if(it != periodicCmdQueue.end())
        {
            periodicCmdQueue.erase(it);
        } else {
            break;
        }
    }
}


void servermain::insertSlowPeriodicCommand(cmds cmd, unsigned char priority)
{
    // TODO: meaningful priority
    // These commands are run every 20 "ticks" of the primary radio command loop
    // Basically 20 times less often than the standard peridic command
    if(priority < 10)
    {
        slowPollCmdQueue.push_front(cmd);
    } else {
        slowPollCmdQueue.push_back(cmd);
    }
}


void servermain::handlePttLimit()
{
    // transmission time exceeded!
    std::cout << "Transmit timeout at 3 minutes. Sending PTT OFF command now.\n";
    issueCmdUniquePriority(cmdSetPTT, false);
    issueDelayedCommand(cmdGetPTT);
}

void servermain::calculateTimingParameters()
{
    // Function for calculating polling parameters.
    // Requires that we know the "baud rate" of the actual
    // radio connection.

    // baud on the serial port reflects the actual rig connection,
    // even if a client-server connection is being used.
    // Computed time for a 10 byte message, with a safety factor of 2.

    if (prefs.serialPortBaud == 0)
    {
        prefs.serialPortBaud = 9600;
        qInfo(logSystem()) << "WARNING: baud rate received was zero. Assuming 9600 baud, performance may suffer.";
    }

    unsigned int usPerByte = 9600*1000 / prefs.serialPortBaud;
    unsigned int msMinTiming=usPerByte * 10*2/1000;
    if(msMinTiming < 25)
        msMinTiming = 25;

    if(haveRigCaps && rigCaps.hasFDcomms)
    {
        delayedCommand->setInterval( msMinTiming); // 20 byte message
    } else {
        delayedCommand->setInterval( msMinTiming * 3); // 20 byte message
    }


    qInfo(logSystem()) << "Delay command interval timing: " << delayedCommand->interval() << "ms";

    // Normal:
    delayedCmdIntervalLAN_ms =  delayedCommand->interval();
    delayedCmdIntervalSerial_ms = delayedCommand->interval();

    // startup initial state:
    delayedCmdStartupInterval_ms =  delayedCommand->interval() * 3;
}

void servermain::receiveBaudRate(quint32 baud)
{
    qInfo() << "Received serial port baud rate from remote server:" << baud;
    prefs.serialPortBaud = baud;
    calculateTimingParameters();
}

void servermain::powerRigOn()
{
    emit sendPowerOn();

    delayedCommand->setInterval(3000); // 3 seconds
    issueDelayedCommand(cmdQueNormalSpeed);
    issueDelayedCommand(cmdStartRegularPolling); // s-meter, etc
    delayedCommand->start();
}

void servermain::powerRigOff()
{
    delayedCommand->stop();
    delayedCmdQue.clear();

    emit sendPowerOff();
}

void servermain::receiveStateInfo(rigstate* state)
{
    qInfo("Setting rig state for wfmain");
    rigState = state;
}


void servermain::handleCtrlC(int sig) {
    if (sig == 2) {
        QCoreApplication::quit();
        //exit(EXIT_FAILURE);
    }
}
