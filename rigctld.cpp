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
            longReply = true;
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
            return;
        }

        if (commandBuffer[num] == "\\")
        {
            num++;
        }

        QStringList command = commandBuffer.mid(num).split(" ");

        if (command[0] == 0xf0 || command[0] == "chk_vfo")
        {
            QString resp;
            if (longReply) {
                resp.append(QString("ChkVFO: "));
            }
            resp.append(QString("%1").arg(rigState->currentVfo));
            response.append(resp);
        }
        else if (command[0] == "dump_state")
        {
            // Currently send "fake" state information until I can work out what is required!
            response.append("1");
            response.append(QString("%1").arg(rigCaps.rigctlModel));
            response.append("0");
            for (bandType band : rigCaps.bands)
            {
                response.append(generateFreqRange(band));
            }
            response.append("0 0 0 0 0 0 0");
            if (rigCaps.hasTransmit) {
                for (bandType band : rigCaps.bands)
                {
                    response.append(generateFreqRange(band));
                }
            }
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
            QString preamps="";
            if (rigCaps.hasPreamp) {
                for (unsigned char pre : rigCaps.preamps)
                {
                    if (pre == 0)
                        continue;
                    preamps.append(QString("%1 ").arg(pre*10));
                }
                if (preamps.endsWith(" "))
                    preamps.chop(1);
            }
            else {
                preamps = "0";
            }
            response.append(preamps);

            QString attens = "";
            if (rigCaps.hasAttenuator) {
                for (unsigned char att : rigCaps.attenuators)
                {
                    if (att == 0)
                        continue;
                    attens.append(QString("%1 ").arg(att,0,16));
                }
                if (attens.endsWith(" "))
                    attens.chop(1);
            }
            else {
                attens = "0";
            }
            response.append(attens);


            response.append("0xffffffffffffffff");
            response.append("0xffffffffffffffff");
            response.append("0xfffffffff7ffffff");
            response.append("0xfffffff083ffffff");
            response.append("0xffffffffffffffff");
            response.append("0xffffffffffffffbf");

            /*
            response.append("0x3effffff");
            response.append("0x3effffff");
            response.append("0x7fffffff");
            response.append("0x7fffffff");
            response.append("0x7fffffff");
            response.append("0x7fffffff");
            */
            response.append("done");
        }

        else if (command[0] == "f" || command[0] == "get_freq")
        {
            QString resp;
            if (longReply) {
                resp.append(QString("Frequency: "));
            }
            if (rigState->currentVfo == 0) {
                resp.append(QString("%1").arg(rigState->vfoAFreq.Hz));
            }
            else {
                resp.append(QString("%1").arg(rigState->vfoBFreq.Hz));
            }
            response.append(resp);
        }
        else if (command[0] == "F" || command[0] == "set_freq")
        {
            setCommand = true;
            freqt freq;
            bool ok=false;
            double newFreq=0.0f;
            QString vfo = "VFOA";
            if (command.length() == 2)
            {
                newFreq = command[1].toDouble(&ok);
            }
            else if (command.length() == 3) // Includes VFO 
            {
                newFreq = command[2].toDouble(&ok);
                vfo = command[1];
            }

            if (ok) {
                freq.Hz = static_cast<int>(newFreq);
                qDebug(logRigCtlD()) << QString("Set frequency: %1 (%2)").arg(freq.Hz).arg(command[1]);
                emit parent->setFrequency(freq);
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
                QString resp;
                if (longReply) {
                    resp.append(QString("PTT: "));
                }
                resp.append(QString("%1").arg(rigState->ptt));
                response.append(resp);
            }
            else
            {
                responseCode = -1;
            }
        }
        else if (command.length() > 1 && (command[0] == "T" || command[0] == "set_ptt"))
        {
            setCommand = true;
            if (rigCaps.hasPTTCommand) {
                if (command[1] == "0") {
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
            QString resp;
            if (longReply) {
                resp.append("VFO: ");
            }

            if (rigState->currentVfo == 0) {
                resp.append("VFOA");
            }
            else {
                resp.append("VFOB");
            }
            response.append(resp);
        }
        else if (command.length() > 1 && (command[0] == "V" || command[0] == "set_vfo"))
        {
            setCommand = true;
            if (command[1] == "?") {
                response.append("set_vfo: ?");
                response.append("VFOA");
                response.append("VFOB");
                response.append("Sub");
                response.append("Main");
                response.append("MEM");
            }
            else if (command[1] == "VFOB" || command[1] == "Sub") {
                emit parent->setVFO(1);
            }
            else {
                emit parent->setVFO(0);
            }
        }
        else if (command[0] == "s" || command[0] == "get_split_vfo")
        {
            if (longReply) {
                response.append(QString("Split: %1").arg(rigState->duplex));
            }
            else {
                response.append(QString("%1").arg(rigState->duplex));
            }

            QString resp;
            if (longReply) {
                resp.append("TX VFO: ");
            }

            if (rigState->currentVfo == 0)
            {
                resp.append(QString("%1").arg("VFOB"));
            }
            else {
                resp.append(QString("%1").arg("VFOA"));
            }
            response.append(resp);
        }
        else if (command.length() > 1 && (command[0] == "S" || command[0] == "set_split_vfo"))
        {
            setCommand = true;
            if (command[1] == "1")
            {
                emit parent->setDuplexMode(dmSplitOn);
                rigState->duplex = dmSplitOn;
            }
            else {
                emit parent->setDuplexMode(dmSplitOff);
                rigState->duplex = dmSplitOff;
            }
        }
        else if (command[0] == "\xf3" || command[0] == "get_vfo_info")
        {
            if (longReply) {
                //response.append(QString("set_vfo: %1").arg(command[1]));

                if (command[1] == "VFOB") {
                    response.append(QString("Freq: %1").arg(rigState->vfoBFreq.Hz));
                }
                else {
                    response.append(QString("Freq: %1").arg(rigState->vfoAFreq.Hz));
                }
                response.append(QString("Mode: %1").arg(getMode(rigState->mode, rigState->datamode)));
                response.append(QString("Width: %1").arg(getFilter(rigState->mode, rigState->filter)));
                response.append(QString("Split: %1").arg(rigState->duplex));
                response.append(QString("SatMode: %1").arg(0)); // Need to get satmode 
            }
            else {
                if (command[1] == "VFOB") {
                    response.append(QString("%1").arg(rigState->vfoBFreq.Hz));
                }
                else {
                    response.append(QString("%1").arg(rigState->vfoAFreq.Hz));
                }
                response.append(QString("%1").arg(getMode(rigState->mode, rigState->datamode)));
                response.append(QString("%1").arg(getFilter(rigState->mode, rigState->filter)));
            }
        }
        else if (command[0] == "i" || command[0] == "get_split_freq")
        {
            QString resp;
            if (longReply) {
                resp.append("TX VFO: ");
            }
            if (rigState->currentVfo == 0) {
                resp.append(QString("%1").arg(rigState->vfoBFreq.Hz));
            }
            else {
                resp.append(QString("%1").arg(rigState->vfoAFreq.Hz));
            }
            response.append(resp);
        }
        else if (command[0] == "I" || command[0] == "set_split_freq")
        {
            setCommand = true;
        }
        else if (command[0] == "m" || command[0] == "get_mode")
        {
            if (longReply) {
                response.append(QString("Mode: %1").arg(getMode(rigState->mode, rigState->datamode)));
                response.append(QString("Passband: %1").arg(getFilter(rigState->mode, rigState->filter)));
            }
            else {
                response.append(QString("%1").arg(getMode(rigState->mode, rigState->datamode)));
                response.append(QString("%1").arg(getFilter(rigState->mode, rigState->filter)));
            }
        }
        else if (command[0] == "M" || command[0] == "set_mode")
        {
            // Set mode
            setCommand = true;
            int width = -1;
            QString vfo = "VFOA";
            QString mode = "USB";
            if (command.length() == 3) {
                width = command[2].toInt();
                mode = command[1];
            }
            else if (command.length() == 4) {
                width = command[3].toInt();
                mode = command[2];
                vfo = command[1];
            }
            qDebug(logRigCtlD()) << "setting mode: VFO:" << vfo << getMode(mode) << mode << "width" << width;

            if (width != -1 && width <= 1800)
                width = 2;
            else
                width = 1;

            emit parent->setMode(getMode(mode), width);
            if (mode.mid(0, 3) == "PKT") {
                emit parent->setDataMode(true, width);
            }
            else {
                emit parent->setDataMode(false, width);
            }
        }
        else if (command[0] == "s" || command[0] == "get_split_vfo")
        {
            if (longReply) {
                response.append(QString("Split: 0"));
                response.append(QString("TX VFO: VFOA"));
            } 
            else
            {
                response.append("0");
                response.append("VFOA");
            }

        }
        else if (command[0] == "j" || command[0] == "get_rit")
        {
            QString resp;
            if (longReply) {
                resp.append("RIT: ");
            }
            resp.append(QString("%1").arg(0));
            response.append(resp);
            }
        else if (command[0] == "J" || command[0] == "set_rit")
        {
            setCommand = true;
        }
        else if (command[0] == "y" || command[0] == "get_ant")
        {
            qInfo(logRigCtlD()) << "get_ant:";

            if (command.length() > 1) {
                if (longReply) {
                    response.append(QString("AntCurr: %1").arg(getAntName((unsigned char)command[1].toInt())));
                    response.append(QString("Option: %1").arg(0));
                    response.append(QString("AntTx: %1").arg(getAntName(rigState->antenna)));
                    response.append(QString("AntRx: %1").arg(getAntName(rigState->antenna)));
                }
                else {
                    response.append(QString("%1").arg(getAntName((unsigned char)command[1].toInt())));
                    response.append(QString("%1").arg(0));
                    response.append(QString("%1").arg(getAntName(rigState->antenna)));
                    response.append(QString("%1").arg(getAntName(rigState->antenna)));
                }
            }
        }
        else if (command[0] == "Y" || command[0] == "set_ant")
        {
        qInfo(logRigCtlD()) << "set_ant:";
        setCommand = true;
        }
        else if (command[0] == "z" || command[0] == "get_xit")
        {
        QString resp;
        if (longReply) {
            resp.append("XIT: ");
        }
        resp.append(QString("%1").arg(0));
        response.append(resp);
        }
        else if (command[0] == "Z" || command[0] == "set_xit")
        {
        setCommand = true;
        }
        else if (command.length() > 1 && (command[0] == "l" || command[0] == "get_level"))
        {
            QString resp;
            int value = 0;
            if (longReply) {
                resp.append("Level Value: ");
            }

            if (command[1] == "STRENGTH") {
                if (rigCaps.model == model7610)
                    value = getCalibratedValue(rigState->sMeter, IC7610_STR_CAL);
                else if (rigCaps.model == model7850)
                    value = getCalibratedValue(rigState->sMeter, IC7850_STR_CAL);
                else
                    value = getCalibratedValue(rigState->sMeter, IC7300_STR_CAL);
                //qInfo(logRigCtlD()) << "Calibration IN:" << rigState->sMeter << "OUT" << value;
                resp.append(QString("%1").arg(value));
            }
            else if (command[1] == "AF") {
                resp.append(QString("%1").arg((float)rigState->afGain / 255.0));
            }
            else if (command[1] == "RF") {
                resp.append(QString("%1").arg((float)rigState->rfGain / 255.0));
            }
            else if (command[1] == "SQL") {
                resp.append(QString("%1").arg((float)rigState->squelch / 255.0));
            }
            else if (command[1] == "COMP") {
                resp.append(QString("%1").arg((float)rigState->compLevel / 255.0));
            }
            else if (command[1] == "MICGAIN") {
                resp.append(QString("%1").arg((float)rigState->micGain / 255.0));
            }
            else if (command[1] == "MON") {
                resp.append(QString("%1").arg((float)rigState->monitorLevel / 255.0));
            }
            else if (command[1] == "VOXGAIN") {
                resp.append(QString("%1").arg((float)rigState->voxGain / 255.0));
            }
            else if (command[1] == "ANTIVOX") {
                resp.append(QString("%1").arg((float)rigState->antiVoxGain / 255.0));
            }
            else if (command[1] == "RFPOWER") {
                resp.append(QString("%1").arg((float)rigState->txPower / 255.0));
            }
            else if (command[1] == "PREAMP") {
                resp.append(QString("%1").arg((float)rigState->preamp / 255.0));
            }
            else if (command[1] == "ATT") {
                resp.append(QString("%1").arg((float)rigState->attenuator / 255.0));
            }
            else {
                resp.append(QString("%1").arg(value));
            }

            response.append(resp);
        }
        else if (command.length() > 2 && (command[0] == "L" || command[0] == "set_level"))
        {
            unsigned char value;
            setCommand = true;
            if (command[1] == "AF") {
                value = command[2].toFloat() * 255;
                emit parent->setAfGain(value);
                rigState->afGain = value;
            }
            else if (command[1] == "RF") {
                value = command[2].toFloat() * 255;
                emit parent->setRfGain(value);
                rigState->rfGain = value;
            }
            else if (command[1] == "SQL") {
                value = command[2].toFloat() * 255;
                emit parent->setSql(value);
                rigState->squelch = value;
            }
            else if (command[1] == "COMP") {
                value = command[2].toFloat() * 255;
                emit parent->setCompLevel(value);
                rigState->compLevel = value;
            }
            else if (command[1] == "MICGAIN") {
                value = command[2].toFloat() * 255;
                emit parent->setMicGain(value);
                rigState->micGain = value;
            }
            else if (command[1] == "MON") {
                value = command[2].toFloat() * 255;
                emit parent->setMonitorLevel(value);
                rigState->monitorLevel = value;
            }
            else if (command[1] == "VOXGAIN") {
                value = command[2].toFloat() * 255;
                emit parent->setVoxGain(value);
                rigState->voxGain = value;
            }
            else if (command[1] == "ANTIVOX") {
                value = command[2].toFloat() * 255;
                emit parent->setAntiVoxGain(value);
                rigState->antiVoxGain = value;
            }
            else if (command[1] == "ATT") {
                value = command[2].toFloat();
                emit parent->setAttenuator(value);
                rigState->attenuator = value;
            }
            else if (command[1] == "PREAMP") {
                value = command[2].toFloat()/10;
                emit parent->setPreamp(value);
                rigState->preamp = value;
            }
            
            qInfo(logRigCtlD()) << "Setting:" << command[1] << command[2] << value;

        }
        else if (command.length()>1 && (command[0] == "u" || command[0] == "get_func"))
        {
            QString resp="";
            bool result = 0;
            if (longReply) {
                resp.append(QString("Func Status: "));
            }
            
            if (command[1] == "FAGC")
            {               
                result=rigState->fagcFunc;
            }
            else if (command[1] == "NB")
            {
                result=rigState->nbFunc;
            }
            else if (command[1] == "COMP")
            {
                result=rigState->compFunc;
            }
            else if (command[1] == "VOX")
            {
                result = rigState->voxFunc;
            }
            else if (command[1] == "TONE")
            {
                result = rigState->toneFunc;
            }
            else if (command[1] == "TSQL")
            {
                result = rigState->tsqlFunc;
            }
            else if (command[1] == "SBKIN")
            {
                result = rigState->sbkinFunc;
            }
            else if (command[1] == "FBKIN")
            {
                result = rigState->fbkinFunc;
            }
            else if (command[1] == "ANF")
            {
                result = rigState->anfFunc;
            }
            else if (command[1] == "NR")
            {
                result = rigState->nrFunc;
            }
            else if (command[1] == "AIP")
            {
                result = rigState->aipFunc;
            }
            else if (command[1] == "APF")
            {
                result = rigState->apfFunc;
            }
            else if (command[1] == "MON")
            {
                result = rigState->monFunc;
            }
            else if (command[1] == "MN")
            {
                result = rigState->mnFunc;
            }
            else if (command[1] == "RF")
            {
                result = rigState->rfFunc;
            }
            else if (command[1] == "ARO")
            {
                result = rigState->aroFunc;
            }
            else if (command[1] == "MUTE")
            {
                result = rigState->muteFunc;
            }
            else if (command[1] == "VSC")
            {
                result = rigState->vscFunc;
            }
            else if (command[1] == "REV")
            {
                result = rigState->revFunc;
            }
            else if (command[1] == "SQL")
            {
                result = rigState->sqlFunc;
            }
            else if (command[1] == "ABM")
            {
                result = rigState->abmFunc;
            }
            else if (command[1] == "BC")
            {
                result = rigState->bcFunc;
            }
            else if (command[1] == "MBC")
            {
                result = rigState->mbcFunc;
            }
            else if (command[1] == "RIT")
            {
                result = rigState->ritFunc;
            }
            else if (command[1] == "AFC")
            {
                result = rigState->afcFunc;
            }
            else if (command[1] == "SATMODE")
            {
                result = rigState->satmodeFunc;
            }
            else if (command[1] == "SCOPE")
            {
                result = rigState->scopeFunc;
            }
            else if (command[1] == "RESUME")
            {
                result = rigState->resumeFunc;
            }
            else if (command[1] == "TBURST")
            {
                result = rigState->tburstFunc;
            }
            else if (command[1] == "TUNER")
            {
                result = rigState->tunerFunc;
            }
            else if (command[1] == "LOCK")
            {
                result = rigState->lockFunc;
            }
            else {
                qInfo(logRigCtlD()) << "Unimplemented func:" << command[0] << command[1];
            }

            resp.append(QString("%1").arg(result));
            response.append(resp);
        }
        else if (command.length() >2 && (command[0] == "U" || command[0] == "set_func"))
        {
            setCommand = true;
            if (command[1] == "FAGC")
            {
                rigState->fagcFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "NB")
            {
                rigState->nbFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "COMP")
            {
                rigState->compFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "VOX")
            {
                rigState->voxFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "TONE")
            {
                rigState->toneFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "TSQL")
            {
                rigState->tsqlFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "SBKIN")
            {
                rigState->sbkinFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "FBKIN")
            {
                rigState->fbkinFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "ANF")
            {
                rigState->anfFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "NR")
            {
                rigState->nrFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "AIP")
            {
                rigState->aipFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "APF")
            {
                rigState->apfFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "MON")
            {
                rigState->monFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "MN")
            {
                rigState->mnFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "RF")
            {
                rigState->rfFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "ARO")
            {
                rigState->aroFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "MUTE")
            {
                rigState->muteFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "VSC")
            {
                rigState->vscFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "REV")
            {
                rigState->revFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "SQL")
            {
                rigState->sqlFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "ABM")
            {
                rigState->abmFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "BC")
            {
                rigState->bcFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "MBC")
            {
                rigState->mbcFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "RIT")
            {
                rigState->ritFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "AFC")
            {
                rigState->afcFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "SATMODE")
            {
                rigState->satmodeFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "SCOPE")
            {
                rigState->scopeFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "RESUME")
            {
                rigState->resumeFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "TBURST")
            {
                rigState->tburstFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "TUNER")
            {
                rigState->tunerFunc = (bool)command[2].toInt();
            }
            else if (command[1] == "LOCK")
            {
                rigState->lockFunc = (bool)command[2].toInt();
            }
            else {
                qInfo(logRigCtlD()) << "Unimplemented func:" << command[0] << command[1] << command[2];
            }
            qInfo(logRigCtlD()) << "Setting:" << command[1] << command[2];
        }
        else if (command.length() > 1 && (command[0] == 0x88 || command[0] == "get_powerstat"))
        {
            
            QString resp;
            if (longReply && command.length() > 1) {
                resp.append(QString("Power Status: "));
            }
            resp.append(QString("%1").arg(1)); // Always reply with ON
            response.append(resp);
            
        }
        else if (command.length() > 1 && (command[0] == 0x87 || command[0] == "set_powerstat"))
        {
            setCommand = true;
            if (command[1] == "0")
            {
                emit parent->sendPowerOff();
            }
            else {
                emit parent->sendPowerOn();
            }
        }
        else {
            qInfo(logRigCtlD()) << "Unimplemented command" << commandBuffer;
        }
        if (longReply) {
            if (command.length() == 2)
                sendData(QString("%1: %2%3").arg(command[0]).arg(command[1]).arg(sep));
            if (command.length() == 3)
                sendData(QString("%1: %2 %3%4").arg(command[0]).arg(command[1]).arg(command[2]).arg(sep));
            if (command.length() == 4)
                sendData(QString("%1: %2 %3 %4%5").arg(command[0]).arg(command[1]).arg(command[2]).arg(command[3]).arg(sep));
        }

        if (setCommand || responseCode != 0 || longReply) {
            if (responseCode == 0) {
                response.append(QString("RPRT 0"));
            }
            else {
                response.append(QString("RPRT %1").arg(responseCode));
            }
        }

        for (QString str : response)
        {
            if (str != "")
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

QString rigCtlClient::generateFreqRange(bandType band)
{
    unsigned int lowFreq = 0;
    unsigned int highFreq = 0;
    switch (band) {
    case band2200m:
        lowFreq = 135000;
        highFreq = 138000;
        break;
    case band630m:
        lowFreq = 493000;
        highFreq = 595000;
        break;
    case band160m:
        lowFreq = 1800000;
        highFreq = 2000000;
        break;
    case band80m:
        lowFreq = 3500000;
        highFreq = 4000000;
        break;
    case band60m:
        lowFreq = 5250000;
        highFreq = 5450000;
        break;
    case band40m:
        lowFreq = 7000000;
        highFreq = 7300000;
        break;
    case band30m:
        lowFreq = 10100000;
        highFreq = 10150000;
        break;
    case band20m:
        lowFreq = 14000000;
        highFreq = 14350000;
        break;
    case band17m:
        lowFreq = 18068000;
        highFreq = 18168000;
        break;
    case band15m:
        lowFreq = 21000000;
        highFreq = 21450000;
        break;
    case band12m:
        lowFreq = 24890000;
        highFreq = 24990000;
        break;
    case band10m:
        lowFreq = 28000000;
        highFreq = 29700000;
        break;
    case band6m:
        lowFreq = 50000000;
        highFreq = 54000000;
        break;
    case band4m:
        lowFreq = 70000000;
        highFreq = 70500000;
        break;
    case band2m:
        lowFreq = 144000000;
        highFreq = 148000000;
        break;
    case band70cm:
        lowFreq = 420000000;
        highFreq = 450000000;
        break;
    case band23cm:
        lowFreq = 1240000000;
        highFreq = 1400000000;
        break;
    case bandAir:
        lowFreq = 108000000;
        highFreq = 137000000;
        break;
    case bandWFM:
        lowFreq = 88000000;
        highFreq = 108000000;
        break;
    case bandGen:
        lowFreq = 10000;
        highFreq = 30000000;
        break;
    }
    QString ret = "";

    if (lowFreq > 0 && highFreq > 0) {
        ret = QString("%1 %2 0x%3 %4 %5 0x%6 0x%7").arg(lowFreq).arg(highFreq).arg(getRadioModes(),0,16).arg(-1).arg(-1).arg(0x16000003,0,16).arg(getAntennas(),0,16);
    }
    return ret;
}

unsigned char rigCtlClient::getAntennas()
{
    unsigned char ant=0;
    for (unsigned char i : rigCaps.antennas)
    {
        ant |= 1<<i;
    }
    return ant;
}

quint64 rigCtlClient::getRadioModes() 
{
    quint64 modes = 0;
    for (mode_info mode : rigCaps.modes)
    {
        for (int i = 0; mode_str[i].str[0] != '\0'; i++)
        {
            QString curMode = mode.name;
            if (!strcmp(curMode.toLocal8Bit(), mode_str[i].str))
            {
                modes |= mode_str[i].mode;
            }
            if (rigCaps.hasDataModes) {
                curMode = "PKT" + mode.name;
                if (!strcmp(curMode.toLocal8Bit(), mode_str[i].str))
                {
                    modes |= mode_str[i].mode;
                }
            }
        }
    }
    return modes;
}

QString rigCtlClient::getAntName(unsigned char ant)
{
    QString ret;
    switch (ant)
    {
        case 0: ret = "ANT1"; break;
        case 1: ret = "ANT2"; break;
        case 2: ret = "ANT3"; break;
        case 3: ret = "ANT4"; break;
        case 4: ret = "ANT5"; break;
        case 30: ret = "ANT_UNKNOWN"; break;
        case 31: ret = "ANT_CURR"; break;
        default: ret = "ANT_UNK"; break;
    }
    return ret;
}

int rigCtlClient::getCalibratedValue(unsigned char meter,cal_table_t cal) {
    
    int interp;

    int i = 0;
    for (i = 0; i < cal.size; i++) {
        if (meter < cal.table[i].raw)
        {
            break;
        }
    }

    if (i == 0)
    {
        return cal.table[0].val;
    } 
    else if (i >= cal.size)
    {
        return cal.table[i - 1].val;
    } 
    else if (cal.table[i].raw == cal.table[i - 1].raw)
    {
        return cal.table[i].val;
    }

    interp = ((cal.table[i].raw - meter)
        * (cal.table[i].val - cal.table[i - 1].val))
        / (cal.table[i].raw - cal.table[i - 1].raw);

    return cal.table[i].val - interp;
}