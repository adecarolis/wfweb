#include "rigctld.h"
#include "logcategories.h"



rigCtlD::rigCtlD(QObject* parent) :
    QTcpServer(parent)
{
}

rigCtlD::~rigCtlD()
{
}

void rigCtlD::receiveFrequency(freqt freq)
{
    emit setFrequency(freq);
}
void rigCtlD::receiveStateInfo(rigStateStruct* state)
{
    qDebug("Setting rig state");
    rigState = state;
}

int rigCtlD::startServer(qint16 port) 
{
    if (!this->listen(QHostAddress::Any, port)) {
        qDebug(logRigCtlD()) << "could not start on port " << port;
        return -1;
    }
    else
    {
        qDebug(logRigCtlD()) << "started on port " << port;
    }
    return 0;
}

void rigCtlD::incomingConnection(qintptr socket) {
    rigCtlClient* client = new rigCtlClient(socket, rigCaps, rigState, this);
    connect(this, SIGNAL(onStopped()), client, SLOT(closeSocket()));
}


void rigCtlD::stopServer() 
{
    qDebug(logRigCtlD()) << "stopping server";
    emit onStopped();
}

void rigCtlD::receiveRigCaps(rigCapabilities caps)
{
    qDebug(logRigCtlD()) << "Got rigcaps for:" << caps.modelName;
    this->rigCaps = caps;
}

rigCtlClient::rigCtlClient(int socketId, rigCapabilities caps,rigStateStruct *state, rigCtlD *parent) : QObject(parent)
{
    commandBuffer.clear();
    sessionId = socketId;
    rigCaps = caps;
    rigState = state;
    socket = new QTcpSocket(this);
    this->parent = parent;
    if (!socket->setSocketDescriptor(sessionId))
    {
        qDebug(logRigCtlD()) << " error binding socket: " << sessionId;
        return;
    }
    connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()), Qt::DirectConnection);
    connect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()), Qt::DirectConnection);
    connect(parent, SIGNAL(sendData(QString)), this, SLOT(sendData(QString)), Qt::DirectConnection);
    qDebug(logRigCtlD()) << " session connected: " << sessionId;
}

void rigCtlClient::socketReadyRead()
{
    QByteArray data = socket->readAll();
    commandBuffer.append(data);
    static QString sep = " ";
    static int num = 0;
    if (commandBuffer.endsWith('\n'))
    {
        // Process command
        if (rigState == Q_NULLPTR)
        {
            qDebug(logRigCtlD()) << "no rigState!";
            return;
        }

        qDebug(logRigCtlD()) << sessionId << "command received" << commandBuffer;
        if (commandBuffer[num] == ";" || commandBuffer[num] == "|" || commandBuffer[num] == ",")
        {
            sep = commandBuffer[num].toLatin1();
            num++;
        }
        else if (commandBuffer[num] == "+")
        {
            sep = "\n";
        }

        if (commandBuffer[num] == "q")
        {
            closeSocket();
        }
        if (commandBuffer.contains("\\chk_vfo"))
        {
            sendData(QString("0\n"));
        }
        if (commandBuffer.contains("\\dump_state"))
        {   
            // Currently send "fake" state information until I can work out what is required!
            sendData(QString("1\n1\n0\n150000.000000 1500000000.000000 0x1ff -1 -1 0x16000003 0xf\n0 0 0 0 0 0 0\n0 0 0 0 0 0 0\n0x1ff 1\n0x1ff 0\n0 0\n0x1e 2400\n0x2 500\n0x1 8000\n0x1 2400\n0x20 15000\n0x20 8000\n0x40 230000\n0 0\n9990\n9990\n10000\n0\n10\n10 20 30\n0x3effffff\n0x3effffff\n0x7fffffff\n0x7fffffff\n0x7fffffff\n0x7fffffff\ndone\n"));
        }
        if (commandBuffer[num] == "f" || commandBuffer.contains("\\get_freq"))
        {
            sendData(QString("%1\n").arg(rigState->vfoAFreq.Hz));
        }
        else if (commandBuffer[num] == "F" || commandBuffer.contains("\\set_freq"))
        {
            const QRegExp rx(QLatin1Literal("[^0-9]+"));
            auto&& parts = commandBuffer.split(rx);
            freqt freq;
            if (parts.length() > 1) {
                freq.Hz = parts[1].toInt();
                emit parent->setFrequency(freq);
            }
            sendData(QString("RPRT 0\n"));
        }
        else if (commandBuffer[num] == "1" || commandBuffer.contains("\\dump_caps"))
        {
            dumpCaps(sep);
        }
        else if (commandBuffer[num] == "t") 
        {
            sendData(QString("%1\n").arg(rigState->ptt));
        }
        else if (commandBuffer[num] == "T") 
        {
            if (commandBuffer[num + 2] == "0") {
                emit parent->setPTT(false);
            }
            else {
                emit parent->setPTT(true);
            }
            sendData(QString("RPRT 0\n"));
        }
        else if (commandBuffer[num] == "v") 
        {
            sendData(QString("VFOA\n"));
        }
        else if (commandBuffer[num] == "m") 
        {
            sendData(QString("%1\n%2\n").arg(getMode(rigState->mode,rigState->datamode)).arg(getFilter(rigState->mode,rigState->filter)));
        }
        else if (commandBuffer[num] == "M")
        {
            // Set mode
            const QRegExp rx(QLatin1Literal("\\s+"));
            auto&& parts = commandBuffer.split(rx);
            if (parts.length() > 1) {
                qDebug(logRigCtlD()) << "setting mode: " << getMode(parts[1]);
                emit parent->setMode(getMode(parts[1]), 0x06);
            }
            sendData(QString("RPRT 0\n"));
        }
        else if (commandBuffer[num] == "s") 
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
    qDebug(logRigCtlD()) << sessionId << "disconnected";
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
        qDebug(logRigCtlD()) << "socket not open!";
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
    switch (mode) {
    case 0:
        return QString("LSB");
    case 1:
        return QString("USB");
    case 2:
        return QString("AM");
    case 3:
        return QString("CW");
    case 4:
        return QString("RTTY");
    case 5:
        return QString("FM");
    case 6:
        return QString("WFM");
    case 7:
        return QString("CWR");
    case 8:
        return QString("RTTYR");
    case 12:
        return QString("PKTUSB");
    case 17:
        return QString("PKTLSB");
    case 22:
        return QString("PKTFM");
    default:
        return QString("");
    }
    return QString("");
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