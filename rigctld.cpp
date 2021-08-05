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
    static QString sep = " ";
    static int num = 0;
    bool longReply = false;
    unsigned char responseCode = 0;
    if (commandBuffer.endsWith('\n'))
    {
        qDebug(logRigCtlD()) << sessionId << "command received" << commandBuffer;
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

        if (command[0] == 0xf0 || command[0]=="chk_vfo")
        {
            sendData(QString("%1\n").arg(rigState->currentVfo));
        }
        else if (command[0] == "dump_state")
        {   
            // Currently send "fake" state information until I can work out what is required!
            sendData(QString("1\n1\n0\n150000.000000 1500000000.000000 0x1ff -1 -1 0x16000003 0xf\n0 0 0 0 0 0 0\n0 0 0 0 0 0 0\n0x1ff 1\n0x1ff 0\n0 0\n0x1e 2400\n0x2 500\n0x1 8000\n0x1 2400\n0x20 15000\n0x20 8000\n0x40 230000\n0 0\n9990\n9990\n10000\n0\n10\n10 20 30\n0x3effffff\n0x3effffff\n0x7fffffff\n0x7fffffff\n0x7fffffff\n0x7fffffff\ndone\n"));
        }
        else if (command[0] == "f" || command[0] == "get_freq")
        {
            if (rigState->currentVfo == 0) {
                sendData(QString("%1\n").arg(rigState->vfoAFreq.Hz));
            }
            else {
                sendData(QString("%1\n").arg(rigState->vfoBFreq.Hz));
            }
        }
        else if (command[0] == "F" || command[0] == "set_freq")
        {
            if (command.length()>1)
            {
                freqt freq;
                bool ok;
                double newFreq = command[1].toDouble(&ok);
                if (ok) {
                    freq.Hz = static_cast<int>(newFreq);
                }
                qInfo(logRigCtlD()) << QString("Set frequency: %1 (%2)").arg(freq.Hz).arg(command[1]);
                emit parent->setFrequency(freq);
            }
            sendData(QString("RPRT %1\n").arg(responseCode));
        }
        else if (command[0] == "1" || command[0] == "dump_caps")
        {
            dumpCaps(sep);
        }
        else if (command[0] == "t" || command[0] == "get_ptt") 
        {
            sendData(QString("%1\n").arg(rigState->ptt));
        }
        else if (command[0] == "T" || command[0] == "set_ptt") 
        {
            if (command.length()>1 && command[1] == "0") {
                emit parent->setPTT(false);
            }
            else {
                emit parent->setPTT(true);
            }
            sendData(QString("RPRT %1\n").arg(responseCode));
        }
        else if (command[0] == "v" || command[0] == "get_vfo")
        {
            if (rigState->currentVfo == 0) {
                sendData(QString("%1\n").arg("VFOA"));
            }
            else {
                sendData(QString("%1\n").arg("VFOB"));
            }
        }
        else if (command[0] == "V" || command[0] == "set_vfo")
        {
            qDebug(logRigCtlD()) << QString("Got VFO command: %1").arg(command[1]);
            if (command.length() > 1 && command[1] == "VFOB") {
                emit parent->setVFO(1);
            }
            else {
                emit parent->setVFO(0);
            }
            sendData(QString("RPRT %1\n").arg(responseCode));
        }
        else if (command[0] == "s" || command[0] == "get_split_vfo")
        {
            sendData(QString("%1\n").arg(rigState->splitEnabled));
            if (rigState->currentVfo == 0)
            {
                sendData(QString("%1\n").arg("VFOB"));
            }
            else {
                sendData(QString("%1\n").arg("VFOA"));
            }
        }
        else if (command[0] == "S" || command[0] == "set_split_vfo")
        {
            if (command.length() > 1 && command[1] == "1")
            {
                emit parent->setSplit(1);
            }
            else {
                emit parent->setSplit(0);
            }
            sendData(QString("RPRT %1\n").arg(responseCode));
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
        sendData(QString("RPRT %1\n").arg(responseCode));
        }
        else if (command[0] == "m" || command[0] == "get_mode")
        {
            if (longReply) {
                sendData(QString("Mode: %1\nPassband: %2\n").arg(getMode(rigState->mode, rigState->datamode)).arg(getFilter(rigState->mode, rigState->filter)));
            }
            else {
                sendData(QString("%1\n%2\n").arg(getMode(rigState->mode, rigState->datamode)).arg(getFilter(rigState->mode, rigState->filter)));
            }
            sendData(QString("RPRT %1\n").arg(responseCode));
        }
        else if (command[0] == "M" || command[0] == "set_mode")
        {
            // Set mode
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
                    emit parent->setDataMode(false, 0x01);
                }
            }
            sendData(QString("RPRT %1\n").arg(responseCode));
        }
        else if (command[0] == "s" || command[0] == "get_split_vfo") 
        {
            sendData(QString("0\nVFOA\n"));
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
    qDebug(logRigCtlD()) << "Sending:" << data;
    if (socket != Q_NULLPTR && socket->isValid() && socket->isOpen())
    {
        socket->write(data.toLatin1());
    }
    else
    {
        qInfo(logRigCtlD()) << "socket not open!";
    }
}

void rigCtlClient::dumpCaps(QString sep)
{
    sendData(QString("Caps dump for model: %1%2").arg(rigCaps.modelID).arg(sep));
    sendData(QString("Model Name:\t%1%2").arg(rigCaps.modelName).arg(sep));
    sendData(QString("Mfg Name:\tIcom%1").arg(sep));
    sendData(QString("Backend version:\t0.1%1").arg(sep));
    sendData(QString("Backend copyright:\t2021%1").arg(sep));
    sendData(QString("Rig type:\tTransceiver%1").arg(sep));
    sendData(QString("PTT type:\tRig capable%1").arg(sep));
    sendData(QString("DCD type:\tRig capable%1").arg(sep));
    sendData(QString("Port type:\tNetwork link%1").arg(sep));
    sendData(QString("\n"));
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
        return ret="PKT";
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

