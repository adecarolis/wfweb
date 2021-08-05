#include "rigctld.h"
#include "logcategories.h"


rigCtlD::rigCtlD(QObject* parent) :
    QTcpServer(parent)
{
}

rigCtlD::~rigCtlD()
{
    qInfo(logRigCtlD()) << "closing rigctld";
}

void rigCtlD::receiveFrequency(freqt freq)
{
    emit setFrequency(freq);
}

void rigCtlD::receiveStateInfo(rigStateStruct* state)
{
    qInfo("Setting rig state");
    rigState = state;
}



int rigCtlD::startServer(qint16 port)
{
    if (!this->listen(QHostAddress::Any, port)) {
        qInfo(logRigCtlD()) << "could not start on port " << port;
        return -1;
    }
    else
    {
        qInfo(logRigCtlD()) << "started on port " << port;
    }
    return 0;
}

void rigCtlD::incomingConnection(qintptr socket) {
    rigCtlClient* client = new rigCtlClient(socket, rigCaps, rigState, this);
    connect(this, SIGNAL(onStopped()), client, SLOT(closeSocket()));
}


void rigCtlD::stopServer()
{
    qInfo(logRigCtlD()) << "stopping server";
    emit onStopped();
}

void rigCtlD::receiveRigCaps(rigCapabilities caps)
{
    qInfo(logRigCtlD()) << "Got rigcaps for:" << caps.modelName;
    this->rigCaps = caps;
}

rigCtlClient::rigCtlClient(int socketId, rigCapabilities caps, rigStateStruct* state, rigCtlD* parent) : QObject(parent)
{

    commandBuffer.clear();
    sessionId = socketId;
    rigCaps = caps;
    rigState = state;
    socket = new QTcpSocket(this);
    this->parent = parent;
    if (!socket->setSocketDescriptor(sessionId))
    {
        qInfo(logRigCtlD()) << " error binding socket: " << sessionId;
        return;
    }
    connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()), Qt::DirectConnection);
    connect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()), Qt::DirectConnection);
    connect(parent, SIGNAL(sendData(QString)), this, SLOT(sendData(QString)), Qt::DirectConnection);
    qInfo(logRigCtlD()) << " session connected: " << sessionId;
}

void rigCtlClient::socketReadyRead()
{
    QByteArray data = socket->readAll();
    commandBuffer.append(data);
    QString sep = "\n";
    static int num = 0;
    bool longReply = false;
    char responseCode = 0;
    QStringList response;
    bool setCommand = false;
    if (commandBuffer.endsWith('\n'))
    {
        qInfo(logRigCtlD()) << sessionId << "command received" << commandBuffer;
        commandBuffer.chop(1); // Remove \n character
        if (commandBuffer.endsWith('\r'))
        {
            commandBuffer.chop(1); // Remove \n character
        }

        // We have a full line so process command.

        if (rigState == Q_NULLPTR)
        {
            qInfo(logRigCtlD()) << "no rigState!";
            return;
        }

        if (commandBuffer[num] == ";" || commandBuffer[num] == "|" || commandBuffer[num] == ",")
        {
            sep = commandBuffer[num].toLatin1();
            num++;
        }
        else if (commandBuffer[num] == "+")
        {
            sep = "\n";
            num++;
        }
        else if (commandBuffer[num] == "#")
        {
            return;
        }
        else if (commandBuffer[num].toLower() == "q")
        {
            closeSocket();
        }

        if (commandBuffer[num] == "\\")
        {
            num++;
            longReply = true;
        }
        QStringList command = commandBuffer.mid(num).split(" ");

        if (command[0] == 0xf0 || command[0] == "chk_vfo")
        {
            response.append(QString("%1").arg(rigState->currentVfo));
        }
        else if (command[0] == "dump_state")
        {
            // Currently send "fake" state information until I can work out what is required!
            response.append("1");
            response.append("1");
            response.append("0");
            response.append("150000.000000 1500000000.000000 0x1ff -1 -1 0x16000003 0xf");
            response.append("0 0 0 0 0 0 0");
            response.append("0 0 0 0 0 0 0");
            response.append("0x1ff 1");
            response.append("0x1ff 0");
            response.append("0 0");
            response.append("0x1e 2400");
            response.append("0x2 500");
            response.append("0x1 8000");
            response.append("0x1 2400");
            response.append("0x20 15000");
            response.append("0x20 8000");
            response.append("0x40 230000");
            response.append("0 0");
            response.append("9900");
            response.append("9900");
            response.append("10000");
            response.append("0");
            response.append("10");
            response.append("10 20 30");
            response.append("0x3effffff");
            response.append("0x3effffff");
            response.append("0x7fffffff");
            response.append("0x7fffffff");
            response.append("0x7fffffff");
            response.append("0x7fffffff");
            response.append("done");

        }
        else if (command[0] == "f" || command[0] == "get_freq")
        {
            if (rigState->currentVfo == 0) {
                response.append(QString("%1").arg(rigState->vfoAFreq.Hz));
            }
            else {
                response.append(QString("%1").arg(rigState->vfoBFreq.Hz));
            }
        }
        else if (command[0] == "F" || command[0] == "set_freq")
        {
            setCommand = true;
            if (command.length() > 1)
            {
                freqt freq;
                bool ok;
                double newFreq = command[1].toDouble(&ok);
                if (ok) {
                    freq.Hz = static_cast<int>(newFreq);
                    qInfo(logRigCtlD()) << QString("Set frequency: %1 (%2)").arg(freq.Hz).arg(command[1]);
                    emit parent->setFrequency(freq);
                }
            }
        }
        else if (command[0] == "1" || command[0] == "dump_caps")
        {
            response.append(QString("Caps dump for model: %1").arg(rigCaps.modelID));
            response.append(QString("Model Name:\t%1").arg(rigCaps.modelName));
            response.append(QString("Mfg Name:\tIcom"));
            response.append(QString("Backend version:\t0.1"));
            response.append(QString("Backend copyright:\t2021"));
            if (rigCaps.hasTransmit) {
                response.append(QString("Rig type:\tTransceiver"));
            }
            else
            {
                response.append(QString("Rig type:\tReceiver"));
            }
            if (rigCaps.hasPTTCommand) {
                response.append(QString("PTT type:\tRig capable"));
            }
            response.append(QString("DCD type:\tRig capable"));
            response.append(QString("Port type:\tNetwork link"));
        }
        else if (command[0] == "t" || command[0] == "get_ptt")
        {
            if (rigCaps.hasPTTCommand) {
                response.append(QString("%1").arg(rigState->ptt));
            }
            else
            {
                responseCode = -1;
            }
        }
        else if (command[0] == "T" || command[0] == "set_ptt")
        {
            setCommand = true;
            if (rigCaps.hasPTTCommand) {
                if (command.length() > 1 && command[1] == "0") {
                    emit parent->setPTT(false);
                }
                else {
                    emit parent->setPTT(true);
                }
            }
            else
            {
                responseCode = -1;
            }
        }
        else if (command[0] == "v" || command[0] == "get_vfo")
        {
            if (rigState->currentVfo == 0) {
                response.append("VFOA");
            }
            else {
                response.append("VFOB");
            }
        }
        else if (command[0] == "V" || command[0] == "set_vfo")
        {
            setCommand = true;
            if (command.length() > 1 && command[1] == "?") {
                response.append("VFOA");
                response.append("VFOB");
            }
            else if (command.length() > 1 && command[1] == "VFOB") {
                emit parent->setVFO(1);
            }
            else {
                emit parent->setVFO(0);
            }
        }
        else if (command[0] == "s" || command[0] == "get_split_vfo")
        {
            response.append(QString("%1").arg(rigState->splitEnabled));
            if (rigState->currentVfo == 0)
            {
                response.append(QString("%1").arg("VFOB"));
            }
            else {
                response.append(QString("%1").arg("VFOA"));
            }
        }
        else if (command[0] == "S" || command[0] == "set_split_vfo")
        {
            setCommand = true;
            if (command.length() > 1 && command[1] == "1")
            {
                emit parent->setSplit(1);
            }
            else {
                emit parent->setSplit(0);
            }
        }
        else if (command[0] == "\xf3" || command[0] == "get_vfo_info")
        {
            if (longReply) {
                response.append(QString("Freq: %1").arg(rigState->vfoAFreq.Hz));
                response.append(QString("Mode: %1").arg(getMode(rigState->mode, rigState->datamode)));
                response.append(QString("Width: %1").arg(getFilter(rigState->mode, rigState->filter)));
                response.append(QString("Split: %1").arg(rigState->splitEnabled));
                response.append(QString("SatMode: %1").arg(0)); // Need to get satmode 
            }
            else {
                response.append(QString("%1").arg(rigState->vfoAFreq.Hz));
                response.append(QString("%1").arg(getMode(rigState->mode, rigState->datamode)));
                response.append(QString("%1").arg(getFilter(rigState->mode, rigState->filter)));
            }
        }
        else if (command[0] == "i" || command[0] == "get_split_freq")
        {
            if (rigState->currentVfo == 0) {
                sendData(QString("%1\n").arg(rigState->vfoBFreq.Hz));
            }
            else {
                sendData(QString("%1\n").arg(rigState->vfoAFreq.Hz));
            }
        }
        else if (command[0] == "I" || command[0] == "set_split_freq")
        {
            setCommand = true;
        }
        else if (command[0] == "m" || command[0] == "get_mode")
        {
            if (longReply) {
                sendData(QString("Mode: %1\nPassband: %2\n").arg(getMode(rigState->mode, rigState->datamode)).arg(getFilter(rigState->mode, rigState->filter)));
            }
            else {
                sendData(QString("%1\n%2\n").arg(getMode(rigState->mode, rigState->datamode)).arg(getFilter(rigState->mode, rigState->filter)));
            }
        }
        else if (command[0] == "M" || command[0] == "set_mode")
        {
            // Set mode
            setCommand = true;
            if (command.length() > 2) {

                qInfo(logRigCtlD()) << "setting mode: " << getMode(command[1]) << command[1] << "width" << command[2];
                int width = command[2].toInt();

                if (width != -1 && width <= 1800)
                    width = 2;
                else
                    width = 1;

                emit parent->setMode(getMode(command[1]), width);
                if (command[1].mid(0, 3) == "PKT") {
                    emit parent->setDataMode(true, width);
                }
                else {
                    emit parent->setDataMode(false, width);
                }
            }
        }
        else if (command[0] == "s" || command[0] == "get_split_vfo")
        {
            response.append(QString("0"));
            response.append(QString("VFOA"));
        }

        if (setCommand == true || responseCode != 0) {
            if (responseCode == 0) {
                response.append(QString("RPRT 0"));
            }
            else {
                response.append(QString("RPRT %1").arg(responseCode));
            }
        }

        for (QString str : response)
        {
            sendData(QString("%1%2").arg(str).arg(sep));
        }

        if (sep != "\n") {
            sendData(QString("\n"));
        }

        commandBuffer.clear();
        sep = " ";
        num = 0;
    }
}

void rigCtlClient::socketDisconnected()
{
    qInfo(logRigCtlD()) << sessionId << "disconnected";
    socket->deleteLater();
    this->deleteLater();
}

void rigCtlClient::closeSocket()
{
    socket->close();
}

void rigCtlClient::sendData(QString data)
{
    qInfo(logRigCtlD()) << "Sending:" << data;
    if (socket != Q_NULLPTR && socket->isValid() && socket->isOpen())
    {
        socket->write(data.toLatin1());
    }
    else
    {
        qInfo(logRigCtlD()) << "socket not open!";
    }
}


QString rigCtlClient::getFilter(unsigned char mode, unsigned char filter) {
    
    if (mode == 3 || mode == 7 || mode == 12 || mode == 17) {
        switch (filter) {
        case 1:
            return QString("1200");
        case 2:
            return QString("500");
        case 3:
            return QString("250");
        }
    }
    else if (mode == 4 || mode == 8)
    {
        switch (filter) {
        case 1:
            return QString("2400");
        case 2:
            return QString("500");
        case 3:
            return QString("250");
        }
    }
    else if (mode == 2)
    {
        switch (filter) {
        case 1:
            return QString("9000");
        case 2:
            return QString("6000");
        case 3:
            return QString("3000");
        }
    }
    else if (mode == 5)
    {
        switch (filter) {
        case 1:
            return QString("15000");
        case 2:
            return QString("10000");
        case 3:
            return QString("7000");
        }
    }
    else { // SSB or unknown mode
        switch (filter) {
        case 1:
            return QString("3000");
        case 2:
            return QString("2400");
        case 3:
            return QString("1800");
        }
    }
    return QString("");
}

QString rigCtlClient::getMode(unsigned char mode, bool datamode) {

    QString ret;
    if (datamode) {
        ret="PKT";
    }

    switch (mode) {
    case 0:
        ret.append("LSB");
        break;
    case 1:
        ret.append("USB");
        break;
    case 2:
        ret.append("AM");
        break;
    case 3:
        ret.append("CW");
        break;
    case 4:
        ret.append("RTTY");
        break;
    case 5:
        ret.append("FM");
        break;
    case 6:
        ret.append("WFM");
        break;
    case 7:
        ret.append("CWR");
        break;
    case 8:
        ret.append("RTTYR");
        break;
    case 12:
        ret.append("USB");
        break;
    case 17:
        ret.append("LSB");
        break;
    case 22:
        ret.append("FM");
        break;
    }
    return ret;
}

unsigned char rigCtlClient::getMode(QString modeString) {

    if (modeString == QString("LSB")) {
        return 0;
    }
    else if (modeString == QString("USB")) {
        return 1;
    }
    else if (modeString == QString("AM")) {
        return 2;
    }
    else if (modeString == QString("CW")) {
        return 3;
    }
    else if (modeString == QString("RTTY")) {
        return 4;
    }
    else if (modeString == QString("FM")) {
        return 5;
    }
    else if (modeString == QString("WFM")) {
        return 6;
    }
    else if (modeString == QString("CWR")) {
        return 7;
    }
    else if (modeString == QString("RTTYR")) {
        return 8;
    }
    else if (modeString == QString("PKTUSB")) {
        return 1;
    }
    else if (modeString == QString("PKTLSB")) {
        return 0;
    }
    else if (modeString == QString("PKTFM")) {
        return 22;
    }
    else {
        return 0;
    }
    return 0;
}

