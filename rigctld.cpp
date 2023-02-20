#include "rigctld.h"
#include "logcategories.h"


static struct
{
    quint64 mode;
    const char* str;
} mode_str[] =
{
    { RIG_MODE_AM, "AM" },
    { RIG_MODE_CW, "CW" },
    { RIG_MODE_USB, "USB" },
    { RIG_MODE_LSB, "LSB" },
    { RIG_MODE_RTTY, "RTTY" },
    { RIG_MODE_FM, "FM" },
    { RIG_MODE_WFM, "WFM" },
    { RIG_MODE_CWR, "CWR" },
    { RIG_MODE_RTTYR, "RTTYR" },
    { RIG_MODE_AMS, "AMS" },
    { RIG_MODE_PKTLSB, "PKTLSB" },
    { RIG_MODE_PKTUSB, "PKTUSB" },
    { RIG_MODE_PKTFM, "PKTFM" },
    { RIG_MODE_PKTFMN, "PKTFMN" },
    { RIG_MODE_ECSSUSB, "ECSSUSB" },
    { RIG_MODE_ECSSLSB, "ECSSLSB" },
    { RIG_MODE_FAX, "FAX" },
    { RIG_MODE_SAM, "SAM" },
    { RIG_MODE_SAL, "SAL" },
    { RIG_MODE_SAH, "SAH" },
    { RIG_MODE_DSB, "DSB"},
    { RIG_MODE_FMN, "FMN" },
    { RIG_MODE_PKTAM, "PKTAM"},
    { RIG_MODE_P25, "P25"},
    { RIG_MODE_DSTAR, "D-STAR"},
    { RIG_MODE_DPMR, "DPMR"},
    { RIG_MODE_NXDNVN, "NXDN-VN"},
    { RIG_MODE_NXDN_N, "NXDN-N"},
    { RIG_MODE_DCR, "DCR"},
    { RIG_MODE_AMN, "AMN"},
    { RIG_MODE_PSK, "PSK"},
    { RIG_MODE_PSKR, "PSKR"},
    { RIG_MODE_C4FM, "C4FM"},
    { RIG_MODE_SPEC, "SPEC"},
    { RIG_MODE_NONE, "" },
};

rigCtlD::rigCtlD(QObject* parent) :
    QTcpServer(parent)
{
}

rigCtlD::~rigCtlD()
{
    qInfo(logRigCtlD()) << "closing rigctld";
}



void rigCtlD::receiveStateInfo(rigstate* state)
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

rigCtlClient::rigCtlClient(int socketId, rigCapabilities caps, rigstate* state, rigCtlD* parent) : QObject(parent)
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
    emit parent->stateUpdated(); // Get the current state.

}

void rigCtlClient::socketReadyRead()
{
    QByteArray data = socket->readAll();
    commandBuffer.append(data);
    QStringList commandList(commandBuffer.split('\n'));
    QString sep = "\n";
    static int num = 0;

    for (QString &commands : commandList)
    {
        bool longReply = false;
        char responseCode = 0;
        QStringList response;
        bool setCommand = false;
        //commands.chop(1); // Remove \n 
        if (commands.endsWith('\r'))
        {
            commands.chop(1); // Remove \n character
        }

        if (commands.isEmpty())
        {
            continue;
        }

        qDebug(logRigCtlD()) << sessionId << "RX:" << commands;

        // We have a full line so process command.

        if (rigState == Q_NULLPTR)
        {
            qInfo(logRigCtlD()) << "no rigState!";
            return;
        }

        if (commands[num] == ';' || commands[num] == '|' || commands[num] == ',')
        {
            sep = commands[num].toLatin1();
            num++;
        }
        else if (commands[num] == '+')
        {
            longReply = true;
            sep = "\n";
            num++;
        }
        else if (commands[num] == '#')
        {
            continue;
        }
        else if (commands[num].toLower() == 'q')
        {
            closeSocket();
            return;
        }

        if (commands[num] == '\\')
        {
            num++;
        }

        QStringList command = commands.mid(num).split(" ");


        if (command[0] == "F" || command[0] == "set_freq")
        {
            setCommand = true;
            freqt freq;
            bool ok=false;
            double newFreq=0.0f;
            quint8 vfo=0;
            if (command.length() == 2)
            {
                newFreq = command[1].toDouble(&ok);
            }
            else if (command.length() == 3) // Includes VFO
            {
                newFreq = command[2].toDouble(&ok);
                if (command[1] == "VFOB")
                {
                    vfo = 1;
                }
            }

            if (ok) {
                freq.Hz = static_cast<int>(newFreq);
                qDebug(logRigCtlD()) << QString("Set frequency: %1 (%2)").arg(freq.Hz).arg(command[1]);
                if (vfo == 0) {
                    rigState->set(VFOAFREQ, (quint64)freq.Hz,true);
                }
                else {
                    rigState->set(VFOBFREQ, (quint64)freq.Hz,true);
                }
            }
        }
        else if (command[0] == "f" || command[0] == "get_freq")
        {
            QString resp;
            if (longReply) {
                resp.append(QString("Frequency: "));
            }

            if (rigState->getChar(CURRENTVFO)==0) {
                resp.append(QString("%1").arg(rigState->getInt64(VFOAFREQ)));
            }
            else {
                resp.append(QString("%1").arg(rigState->getInt64(VFOBFREQ)));
            }

            response.append(resp);
        }
        else if (command[0] == "M" || command[0] == "set_mode")
        {
            // Set mode
            setCommand = true;
            quint8 width = 0;
            quint16 passband = 0;
            QString vfo = "VFOA";
            QString mode = "USB";
            if (command.length() == 2) {
                mode = command[1];
            }
            else if (command.length() == 3) {
                passband = command[2].toInt();
                mode = command[1];
            }
            else if (command.length() == 4) {
                passband = command[3].toInt();
                mode = command[2];
                vfo = command[1];
            }

            qDebug(logRigCtlD()) << "setting mode: VFO:" << vfo << getMode(mode) << mode << "passband" << passband << "command:" << commands;

            if (!mode.isEmpty())
            {
                rigState->set(MODE, getMode(mode), true);
                if (mode.mid(0, 3) == "PKT") {
                    rigState->set(DATAMODE, true, true);
                }
                else {
                    rigState->set(DATAMODE, false, true);
                }
            }

            if (passband > 0)
            {
                switch ((mode_kind)getMode(mode)) {

                case modeAM:
                    if (passband > 6000) {
                        width = 1;
                    }
                    else if (passband > 3000 && passband <= 6000) {
                        width = 2;
                    }
                    else if (passband <= 3000) {
                        width = 3;
                    }
                    break;

                case modeFM:
                    if (passband > 10000) {
                        width = 1;
                    }
                    else if (passband > 7000 && passband <= 10000) {
                        width = 2;
                    }
                    else if (passband <= 7000) {
                        width = 3;
                    }
                    break;

                case modeCW:
                case modeRTTY:
                case modeCW_R:
                case modeRTTY_R:
                case modePSK:
                    if (passband > 500) {
                        width = 1;
                    }
                    else if (passband > 250 && passband <= 500) {
                        width = 2;
                    }
                    else if (passband <= 250) {
                        width = 3;
                    }
                    break;

                default:
                    if (passband > 2400) {
                        width = 1;
                    }
                    else if (passband > 1800 && passband <= 2400) {
                        width = 2;
                    }
                    else if (passband <= 1800) {
                        width = 3;
                    }
                    break;

                }
                rigState->set(FILTER, width, true);
                rigState->set(PASSBAND, passband, true);
            }
        }
        else if (command[0] == "m" || command[0] == "get_mode")
        {
            if (longReply) {
                response.append(QString("TX Mode: %1").arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))));
                response.append(QString("TX Passband: %1").arg(rigState->getUInt16(PASSBAND)));
            }
            else {
                response.append(QString("%1").arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))));
                response.append(QString("%1").arg(rigState->getUInt16(PASSBAND)));
            }
            //qDebug(logRigCtlD()) << QString("get_mode: %1 passband: %2").arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))).arg(rigState->getUInt16(PASSBAND));
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
            else if (command[1] == "VFOA" || command[1] == "Main")
            {
                rigState->set(CURRENTVFO, (quint8)0, true);
            }
            else if (command[1] == "VFOB" || command[1] == "Sub")
            {
                rigState->set(CURRENTVFO, (quint8)1, true);
            }
            else if (command[1] == "MEM")
            {
                rigState->set(CURRENTVFO, (quint8)2, true);
            }
        }
        else if (command[0] == "v" || command[0] == "v\nv" || command[0] == "get_vfo")
        {
        QString resp;
            if (longReply) {
                resp.append("VFO: ");
            }

            if (rigState->getChar(CURRENTVFO) == 0) {
                resp.append("VFOA");
            }
            else if (rigState->getChar(CURRENTVFO) == 1) {
                resp.append("VFOB");
            }
            else if (rigState->getChar(CURRENTVFO) == 2) {
                resp.append("MEM");
            }

            response.append(resp);
        }
        else if (command[0] == "J" || command[0] == "set_rit")
        {
            rigState->set(RITVALUE, command[1].toInt(),true);
            setCommand = true;
        }
        else if (command[0] == "j" || command[0] == "get_rit")
        {
            QString resp;
            if (longReply) {
                resp.append("RIT: ");
            }
            resp.append(QString("%1").arg(rigState->getInt32(RITVALUE)));
            response.append(resp);
        }
        else if (command[0] == "Z" || command[0] == "set_xit")
        {
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
        else if (command.length() > 1 && (command[0] == "T" || command[0] == "set_ptt"))
        {
            setCommand = true;
            if (rigCaps.hasPTTCommand) {
                rigState->set(PTT, (bool)command[1].toInt(), true);
            }
            else
            {
                responseCode = -1;
            }
        }
        else if (command[0] == "t" || command[0] == "get_ptt")
        {
            if (rigCaps.hasPTTCommand) {
                QString resp;
                if (longReply) {
                    resp.append(QString("PTT: "));
                }
                resp.append(QString("%1").arg(rigState->getBool(PTT)));
                response.append(resp);
            }
            else
            {
                responseCode = -1;
            }
        }
        else if (command.length() > 0 && (command[0] == "0x0b" || command[0] == "get_dcd"))
        {
            response.append(QString("%1").arg((float)rigState->getChar(SQUELCH) / 255.0));
        }
        else if (command.length() > 0 && (command[0] == "R" || command[0] == "set_rptr_shift"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "r" || command[0] == "get_rptr_shift"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "O" || command[0] == "set_rptr_offs"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "o" || command[0] == "get_rptr_offs"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "C" || command[0] == "set_ctcss_tone"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "c" || command[0] == "get_ctcss_tone"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "D" || command[0] == "set_dcs_tone"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "d" || command[0] == "get_dcs_tone"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "\x90" || command[0] == "set_ctcss_sql"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "\x91" || command[0] == "get_ctcss_sql"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "\x92" || command[0] == "set_dcs_sql"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "\x93" || command[0] == "get_dcs_sql"))
        {
            responseCode = -11;
        }
        else if (command[0] == "i" || command[0] == "get_split_freq")
        {
            QString resp;
            if (longReply) {
                resp.append("TX VFO: ");
            }

            if (rigState->getInt64(CURRENTVFO) == 0) {
                resp.append(QString("%1").arg(rigState->getInt64(VFOBFREQ)));
            }
            else {
                resp.append(QString("%1").arg(rigState->getInt64(VFOAFREQ)));
            }

            response.append(resp);
        }
        else if (command.length() > 1 && (command[0] == "I" || command[0] == "set_split_freq"))
        {
            setCommand = true;
            bool ok = false;
            double newFreq = 0.0f;
            newFreq = command[1].toDouble(&ok);
            if (ok) {
                qDebug(logRigCtlD()) << QString("set_split_freq: %1 (%2)").arg(newFreq).arg(command[1]);
                rigState->set(VFOBFREQ, static_cast<quint64>(newFreq),false);
            }
        }
        else if (command.length() > 2 && (command[0] == "X" || command[0] == "set_split_mode"))
        {
            setCommand = true;
        }

        else if (command.length() > 0 && (command[0] == "x" || command[0] == "get_split_mode"))
        {
            if (longReply) {
                response.append(QString("TX Mode: %1").arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))));
                response.append(QString("TX Passband: %1").arg(rigState->getUInt16(PASSBAND)));
            }
            else {
                response.append(QString("%1").arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))));
                response.append(QString("%1").arg(rigState->getUInt16(PASSBAND)));
            }
        }
        else if (command.length() > 1 && (command[0] == "S" || command[0] == "set_split_vfo"))
        {
            setCommand = true;

            if (command[1] == "1")
            {
                rigState->set(DUPLEX, dmSplitOn, true);
            }
            else {
                rigState->set(DUPLEX, dmSplitOff, true);
            }
        }
        else if (command[0] == "s" || command[0] == "get_split_vfo")
        {

            if (longReply) {
                response.append(QString("Split: %1").arg(rigState->getChar(DUPLEX)));
            }
            else {
                response.append(QString("%1").arg(rigState->getChar(DUPLEX)));
            }

            QString resp;
            if (longReply) {
                resp.append("TX VFO: ");
            }


            if (rigState->getChar(CURRENTVFO) == 0)
            {
                resp.append(QString("%1").arg("VFOB"));
            }
            else {
                resp.append(QString("%1").arg("VFOA"));
            }

            response.append(resp);
        }
        else if (command.length() > 0 && (command[0] == "N" || command[0] == "set_ts"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "n" || command[0] == "get_ts"))
        {
            responseCode = -11;
        }
        else if (command.length() >2 && (command[0] == "U" || command[0] == "set_func"))
        {
            setCommand = true;

            if (command[1] == "FAGC")
            {
                rigState->set(FAGCFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "NB")
            {
                rigState->set(NBFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "COMP")
            {
                rigState->set(COMPFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "VOX")
            {
                rigState->set(VOXFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "TONE")
            {
                rigState->set(TONEFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "TSQL")
            {
                rigState->set(TSQLFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "SBKIN")
            {
                rigState->set(SBKINFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "FBKIN")
            {
                rigState->set(FBKINFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "ANF")
            {
                rigState->set(ANFFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "NR")
            {
                rigState->set(NRFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "AIP")
            {
                rigState->set(AIPFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "APF")
            {
                rigState->set(APFFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "MON")
            {
                rigState->set(MONFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "MN")
            {
                rigState->set(MNFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "RF")
            {
                rigState->set(RFFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "ARO")
            {
                rigState->set(AROFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "MUTE")
            {
                rigState->set(MUTEFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "VSC")
            {
                rigState->set(VSCFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "REV")
            {
                rigState->set(REVFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "SQL")
            {
                rigState->set(SQLFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "ABM")
            {
                rigState->set(ABMFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "BC")
            {
                rigState->set(BCFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "MBC")
            {
                rigState->set(MBCFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "RIT")
            {
                rigState->set(RITFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "AFC")
            {
                rigState->set(AFCFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "SATMODE")
            {
                rigState->set(SATMODEFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "SCOPE")
            {
                rigState->set(SCOPEFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "RESUME")
            {
                rigState->set(RESUMEFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "TBURST")
            {
                rigState->set(TBURSTFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "TUNER")
            {
                rigState->set(TUNERFUNC, (quint8)command[2].toInt(), true);
            }
            else if (command[1] == "LOCK")
            {
                rigState->set(LOCKFUNC, (quint8)command[2].toInt(), true);
            }
            else {
                qInfo(logRigCtlD()) << "Unimplemented func:" << command[0] << command[1] << command[2];
            }

            qInfo(logRigCtlD()) << "Setting:" << command[1] << command[2];
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
                result=rigState->getBool(FAGCFUNC);
            }
            else if (command[1] == "NB")
            {
                result = rigState->getBool(NBFUNC);
            }
            else if (command[1] == "COMP")
            {
                result=rigState->getBool(COMPFUNC);
            }
            else if (command[1] == "VOX")
            {
                result = rigState->getBool(VOXFUNC);
            }
            else if (command[1] == "TONE")
            {
                result = rigState->getBool(TONEFUNC);
            }
            else if (command[1] == "TSQL")
            {
                result = rigState->getBool(TSQLFUNC);
            }
            else if (command[1] == "SBKIN")
            {
                result = rigState->getBool(SBKINFUNC);
            }
            else if (command[1] == "FBKIN")
            {
                result = rigState->getBool(FBKINFUNC);
            }
            else if (command[1] == "ANF")
            {
                result = rigState->getBool (ANFFUNC);
            }
            else if (command[1] == "NR")
            {
                result = rigState->getBool(NRFUNC);
            }
            else if (command[1] == "AIP")
            {
                result = rigState->getBool(AIPFUNC);
            }
            else if (command[1] == "APF")
            {
                result = rigState->getBool(APFFUNC);
            }
            else if (command[1] == "MON")
            {
                result = rigState->getBool(MONFUNC);
            }
            else if (command[1] == "MN")
            {
                result = rigState->getBool(MNFUNC);
            }
            else if (command[1] == "RF")
            {
                result = rigState->getBool(RFFUNC);
            }
            else if (command[1] == "ARO")
            {
                result = rigState->getBool(AROFUNC);
            }
            else if (command[1] == "MUTE")
            {
                result = rigState->getBool(MUTEFUNC);
            }
            else if (command[1] == "VSC")
            {
                result = rigState->getBool(VSCFUNC);
            }
            else if (command[1] == "REV")
            {
                result = rigState->getBool(REVFUNC);
            }
            else if (command[1] == "SQL")
            {
                result = rigState->getBool(SQLFUNC);
            }
            else if (command[1] == "ABM")
            {
                result = rigState->getBool(ABMFUNC);
            }
            else if (command[1] == "BC")
            {
                result = rigState->getBool(BCFUNC);
            }
            else if (command[1] == "MBC")
            {
                result = rigState->getBool(MBCFUNC);
            }
            else if (command[1] == "RIT")
            {
                result = rigState->getBool(RITFUNC);
            }
            else if (command[1] == "AFC")
            {
                result = rigState->getBool(AFCFUNC);
            }
            else if (command[1] == "SATMODE")
            {
                result = rigState->getBool(SATMODEFUNC);
            }
            else if (command[1] == "SCOPE")
            {
                result = rigState->getBool(SCOPEFUNC);
            }
            else if (command[1] == "RESUME")
            {
                result = rigState->getBool(RESUMEFUNC);
            }
            else if (command[1] == "TBURST")
            {
                result = rigState->getBool(TBURSTFUNC);
            }
            else if (command[1] == "TUNER")
            {
                result = rigState->getBool(TUNERFUNC);
            }
            else if (command[1] == "LOCK")
            {
            result = rigState->getBool(LOCKFUNC);
            }
            else {
                qInfo(logRigCtlD()) << "Unimplemented func:" << command[0] << command[1];
            }

            resp.append(QString("%1").arg(result));
            response.append(resp);
        }
        else if (command.length() > 2 && (command[0] == "L" || command[0] == "set_level"))
        {
            int value=0;
            setCommand = true;
            if (command[1] == "AF") {
                value = command[2].toFloat() * 255;
                rigState->set(AFGAIN, quint8(value), true);
            }
            else if (command[1] == "RF") {
                value = command[2].toFloat() * 255;
                rigState->set(RFGAIN, quint8(value), true);
            }
            else if (command[1] == "RFPOWER") {
                value = command[2].toFloat() * 255;
                rigState->set(RFPOWER, quint8(value), true);
            }
            else if (command[1] == "SQL") {
                value = command[2].toFloat() * 255;
                rigState->set(SQUELCH, quint8(value), true);
            }
            else if (command[1] == "COMP") {
                value = command[2].toFloat() * 255;
                rigState->set(COMPLEVEL, quint8(value), true);
            }
            else if (command[1] == "MICGAIN") {
                value = command[2].toFloat() * 255;
                rigState->set(MICGAIN, quint8(value), true);
            }
            else if (command[1] == "MON") {
                value = command[2].toFloat() * 255;
                rigState->set(MONITORLEVEL, quint8(value), true);
            }
            else if (command[1] == "VOXGAIN") {
                value = command[2].toFloat() * 255;
                rigState->set(VOXGAIN, quint8(value), true);
            }
            else if (command[1] == "ANTIVOX") {
                value = command[2].toFloat() * 255;
                rigState->set(ANTIVOXGAIN, quint8(value), true);
            }
            else if (command[1] == "ATT") {
                value = command[2].toInt();
                rigState->set(ATTENUATOR, quint8(value), true);
            }
            else if (command[1] == "PREAMP") {
                value = command[2].toFloat() / 10;
                rigState->set(PREAMP, quint8(value), true);
            }
            else if (command[1] == "AGC") {
                value = command[2].toFloat() * 255;
                rigState->set(AGC, quint8(value), true);
            }
            else if (command[1] == "CWPITCH") {
                value = command[2].toInt();
                rigState->set(CWPITCH, value, true);
            }
            else if (command[1] == "NOTCHF") {
                value = command[2].toInt();
                rigState->set(NOTCHF, value, true);
            }
            else if (command[1] == "IF") {
                value = command[2].toInt();
                rigState->set(IF, qint16(value), true);
            }
            else if (command[1] == "PBT_IN") {
                value = command[2].toFloat() * 255;
                rigState->set(PBTIN, quint8(value), true);
            }
            else if (command[1] == "PBT_OUT") {
                value = command[2].toFloat() * 255;
                rigState->set(PBTOUT, quint8(value), true);
            }
            else if (command[1] == "APF") {
                value = command[2].toFloat() * 255;
                rigState->set(APF, quint8(value), true);
            }
            else if (command[1] == "NR") {
                value = command[2].toFloat() * 255;
                rigState->set(NR, quint8(value), true);
            }
            else if (command[1] == "BAL") {
                value = command[2].toFloat() * 255;
                rigState->set(BAL, quint8(value), true);
            }
            else if (command[1] == "KEYSPD") {
                value = command[2].toInt() * 5.1;
                rigState->set(KEYSPD, quint8(value), true);
            }

            qInfo(logRigCtlD()) << "Setting:" << command[1] << command[2] << value;

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
                    value = getCalibratedValue(rigState->getChar(SMETER), IC7610_STR_CAL);
                else if (rigCaps.model == model7850)
                    value = getCalibratedValue(rigState->getChar(SMETER), IC7850_STR_CAL);
                else
                    value = getCalibratedValue(rigState->getChar(SMETER), IC7300_STR_CAL);
                //qInfo(logRigCtlD()) << "Calibration IN:" << rigState->sMeter << "OUT" << value;
                resp.append(QString("%1").arg(value));
            }

            else if (command[1] == "AF") {
                resp.append(QString("%1").arg((float)rigState->getChar(AFGAIN) / 255.0));
            }
            else if (command[1] == "RF") {
                resp.append(QString("%1").arg((float)rigState->getChar(RFGAIN) / 255.0));
            }
            else if (command[1] == "SQL") {
                resp.append(QString("%1").arg((float)rigState->getChar(SQUELCH) / 255.0));
            }
            else if (command[1] == "COMP") {
                resp.append(QString("%1").arg((float)rigState->getChar(COMPLEVEL) / 255.0));
            }
            else if (command[1] == "MICGAIN") {
                resp.append(QString("%1").arg((float)rigState->getChar(MICGAIN) / 255.0));
            }
            else if (command[1] == "MON") {
                resp.append(QString("%1").arg((float)rigState->getChar(MONITORLEVEL) / 255.0));
            }
            else if (command[1] == "VOXGAIN") {
                resp.append(QString("%1").arg((float)rigState->getChar(VOXGAIN) / 255.0));
            }
            else if (command[1] == "ANTIVOX") {
                resp.append(QString("%1").arg((float)rigState->getChar(ANTIVOXGAIN) / 255.0));
            }
            else if (command[1] == "RFPOWER") {
                resp.append(QString("%1").arg((float)rigState->getChar(RFPOWER) / 255.0));
            }
            else if (command[1] == "PREAMP") {
                resp.append(QString("%1").arg(rigState->getChar(PREAMP)*10));
            }
            else if (command[1] == "ATT") {
                resp.append(QString("%1").arg(rigState->getChar(ATTENUATOR)));
            }
            else if (command[1] == "CWPITCH") {
                resp.append(QString("%1").arg(rigState->getInt16(CWPITCH)));
            }
            else if (command[1] == "NOTCHF") {
                resp.append(QString("%1").arg(rigState->getInt16(NOTCHF)));
            }
            else if (command[1] == "IF") {
                resp.append(QString("%1").arg(rigState->getInt16(IF)));
            }
            else if (command[1] == "PBT_IN") {
                resp.append(QString("%1").arg((float)rigState->getChar(PBTIN) / 255.0));
            }
            else if (command[1] == "PBT_OUT") {
                resp.append(QString("%1").arg((float)rigState->getChar(PBTOUT) / 255.0));
            }
            else if (command[1] == "APF") {
                resp.append(QString("%1").arg((float)rigState->getChar(APF) / 255.0));
            }
            else if (command[1] == "NR") {
                resp.append(QString("%1").arg((float)rigState->getChar(NR) / 255.0));
            }
            else if (command[1] == "BAL") {
                resp.append(QString("%1").arg((float)rigState->getChar(BAL) / 255.0));
            }
            else if (command[1] == "KEYSPD") {
                resp.append(QString("%1").arg(rigState->getChar(KEYSPD)/5.1));
            }
            else {
                resp.append(QString("%1").arg(value));
            }

            response.append(resp);
        }

        else if (command.length() > 2 && (command[0] == "P" || command[0] == "set_parm"))
        {
            int value=0;
            setCommand = true;
            if (command[1] == "ANN") {
                value = command[2].toInt();
                rigState->set(ANN, quint8(value), true);
            }
            else if (command[1] == "APO") {
                value = command[2].toInt();
                rigState->set(APO, quint8(value), true);
            }
            else if (command[1] == "BACKLIGHT") {
                value = command[2].toFloat() * 255;
                rigState->set(BACKLIGHT, quint8(value), true);
            }
            else if (command[1] == "BEEP") {
                value = command[2].toInt();
                rigState->set(BEEP, quint8(value), true);
            }
            else if (command[1] == "TIME") {
                value = command[2].toLongLong();
                rigState->set(TIME, value, true);
            }
            else if (command[1] == "BAT") {
                value = command[2].toFloat() * 255;
                rigState->set(BAT, quint8(value), true);
            }
            else if (command[1] == "KEYLIGHT") {
                value = command[2].toFloat() * 255;
                rigState->set(KEYLIGHT, quint8(value), true);
            }
            else {
                qInfo(logRigCtlD()) << "Unimplemented parm:" << command[0] << command[1];
            }

            qInfo(logRigCtlD()) << "Setting:" << command[1] << command[2] << value;

        }
        else if (command.length() > 1 && (command[0] == "p" || command[0] == "get_parm"))
        {
            QString resp;
            if (longReply) {
                resp.append(QString("get_parm: %1%2").arg(command[1]).arg(sep));
            }

            if (command[1] == "ANN") {
                resp.append(QString("%1").arg(rigState->getChar(ANN)));
            }
            else if (command[1] == "APO") {
                resp.append(QString("%1").arg(rigState->getChar(APO)));
            }
            else if (command[1] == "BACKLIGHT") {
                resp.append(QString("%1").arg((float)rigState->getChar(BACKLIGHT) / 255.0));
            }
            else if (command[1] == "BEEP") {
                resp.append(QString("%1").arg(rigState->getChar(BEEP)));
            }
            else if (command[1] == "TIME") {
                resp.append(QString("%1").arg(rigState->getInt64(TIME)));
            }
            else if (command[1] == "BAT") {
                resp.append(QString("%1").arg((float)rigState->getChar(BAT) / 255.0));
            }
            else if (command[1] == "KEYLIGHT") {
                resp.append(QString("%1").arg((float)rigState->getChar(KEYLIGHT) / 255.0));
            }
            else {
                qInfo(logRigCtlD()) << "Unimplemented parm:" << command[0] << command[1];
            }

            response.append(resp);
        }
        else if (command.length() > 0 && (command[0] == "B" || command[0] == "set_bank"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "b" || command[0] == "get_bank"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "E" || command[0] == "set_mem"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "e" || command[0] == "get_mem"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "G" || command[0] == "vfo_op"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "g" || command[0] == "scan"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "H" || command[0] == "set_channel"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "A" || command[0] == "set_trn"))
        {
            responseCode = -11;
        }
        else if (command.length() > 0 && (command[0] == "a" || command[0] == "get_trn"))
        {
            responseCode = -11;
        }
        else if (command.length() > 1 && (command[0] == "Y" || command[0] == "set_ant"))
        {
            setCommand = true;
            qInfo(logRigCtlD()) << "set_ant:" << command[1];
            rigState->set(ANTENNA,antFromName(command[1]),true);
        }
        else if (command[0] == "y" || command[0] == "get_ant")
        {
            qInfo(logRigCtlD()) << "get_ant:";

            if (command.length() > 1) {
                if (longReply) {
                    response.append(QString("AntCurr: %1").arg(getAntName((quint8)command[1].toInt())));
                    response.append(QString("Option: %1").arg(0));
                    response.append(QString("AntTx: %1").arg(getAntName(rigState->getChar(ANTENNA))));
                    response.append(QString("AntRx: %1").arg(getAntName(rigState->getChar(ANTENNA))));
                }
                else {
                    response.append(QString("%1").arg(getAntName((quint8)command[1].toInt())));
                    response.append(QString("%1").arg(0));
                    response.append(QString("%1").arg(getAntName(rigState->getChar(ANTENNA))));
                    response.append(QString("%1").arg(getAntName(rigState->getChar(ANTENNA))));
                }
            }
        }
        else if (command.length() > 0 && (command[0] == "*" || command[0] == "reset"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 0 && (command[0] == "b" || command[0] == "send_morse"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 1 && (command[0] == '\x87' || command[0] == "set_powerstat"))
        {
            setCommand = true;
            if (command[1] == "0")
            {
                rigState->set(POWERONOFF, false, true);
            }
            else {
                rigState->set(POWERONOFF, true, true);
            }
        }
        else if (command.length() > 0 && (command[0] == '\x88' || command[0] == "get_powerstat"))
        {

            QString resp;
            if (longReply) {
                resp.append(QString("Power Status: "));
            }
            resp.append(QString("%1").arg(1)); // Always reply with ON
            response.append(resp);

        }
        else if (command.length() > 0 && (command[0] == "\x89" || command[0] == "send_dtmf"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 0 && (command[0] == "\x8a" || command[0] == "recv_dtmf"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 0 && (command[0] == "_" || command[0] == "get_info"))
        {
            response.append("None");
        }
        else if (command.length() > 0 && (command[0] == '\xf5' || command[0] == "get_rig_info"))
        {
            duplexMode split = rigState->getDuplex(DUPLEX);
            quint8 rxa = 1;
            quint8 txa = split == 0;
            quint8 rxb = !rxa;
            quint8 txb = split == 1;
            QString resp = QString("VFO=%0 Freq=%1 Mode=%2 Width=%3 RX=%4 TX=%5\nVFO=%6 Freq=%7 Mode=%8 Width=%9 RX=%10 TX=%11\nSplit=%12 SatMode=%13\nRig=%14\nApp=wfview\nVersion=%15\n")
                .arg(getVfoName(0)).arg(rigState->getInt64(VFOAFREQ)).arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))).arg(rigState->getUInt16(PASSBAND)).arg(rxa).arg(txa)
                .arg(getVfoName(1)).arg(rigState->getInt64(VFOBFREQ)).arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))).arg(rigState->getUInt16(PASSBAND)).arg(rxb).arg(txb)
                .arg(split).arg(rigState->getChar(SATMODEFUNC)).arg(rigCaps.modelName).arg(WFVIEW_VERSION);
            unsigned long crc = doCrc((unsigned char*)resp.toStdString().c_str(), resp.length());
            resp = resp + QString("CRC=0x%0").arg(crc, 8, 16, QLatin1Char('0'));
            response.append(resp);
        }
        else if (command[0] == "\xf3" || command[0] == "get_vfo_info")
        {
            if (longReply) {
                if (command[1] == "?") {
                    if (rigState->getChar(CURRENTVFO) == 0) {
                        response.append(QString("set_vfo: VFOA"));
                    }
                    else
                    {
                        response.append(QString("set_vfo: VFOB"));
                    }
                }
                if (command[1] == "VFOB") {
                    response.append(QString("Freq: %1").arg(rigState->getInt64(VFOBFREQ)));
                }
                else {
                    response.append(QString("Freq: %1").arg(rigState->getInt64(VFOAFREQ)));
                }
                response.append(QString("Mode: %1").arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))));
                response.append(QString("Width: %1").arg(rigState->getUInt16(PASSBAND)));

                response.append(QString("Split: %1").arg(rigState->getDuplex(DUPLEX)));
                response.append(QString("SatMode: %1").arg(0)); // Need to get satmode
            }
            else {
                if (command[1] == "VFOB") {
                    response.append(QString("%1").arg(rigState->getInt64(VFOBFREQ)));
                }
                else {
                    response.append(QString("%1").arg(rigState->getInt64(VFOAFREQ)));
                }
                response.append(QString("%1").arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))));
                response.append(QString("%1").arg(rigState->getUInt16(PASSBAND)));

            }
        }
        else if (command[0] == "dump_state")
        {
            quint64 modes = getRadioModes();

            // rigctld protocol version
            response.append("1");
            // Radio model
            response.append(QString("%1").arg(rigCaps.rigctlModel));
            // Print something (used to be ITU region)
            response.append("0");
            // Supported RX bands (startf,endf,modes,low_power,high_power,vfo,ant)
            quint32 lowFreq = 0;
            quint32 highFreq = 0;
            for (bandType band : rigCaps.bands)
            {
                if (lowFreq == 0 || band.lowFreq < lowFreq)
                    lowFreq = band.lowFreq;
                if (band.highFreq > highFreq)
                    highFreq = band.highFreq;
            }
            response.append(QString("%1.000000 %2.000000 0x%3 %4 %5 0x%6 0x%7").arg(lowFreq).arg(highFreq)
                .arg(modes, 0, 16).arg(-1).arg(-1).arg(0x16000000, 0, 16).arg(getAntennas(), 0, 16));
            response.append("0 0 0 0 0 0 0");

            if (rigCaps.hasTransmit) {
                // Supported TX bands (startf,endf,modes,low_power,high_power,vfo,ant)
                for (bandType band : rigCaps.bands)
                {
                    response.append(QString("%1.000000 %2.000000 0x%3 %4 %5 0x%6 0x%7").arg(band.lowFreq).arg(band.highFreq)
                        .arg(modes, 0, 16).arg(2000).arg(100000).arg(0x16000000, 0, 16).arg(getAntennas(), 0, 16));
                }
            }
            response.append("0 0 0 0 0 0 0");

            response.append(QString("0x%1 1").arg(modes, 0, 16));
            response.append(QString("0x%1 10").arg(modes, 0, 16));
            response.append(QString("0x%1 100").arg(modes, 0, 16));
            response.append(QString("0x%1 1000").arg(modes, 0, 16));
            response.append(QString("0x%1 2500").arg(modes, 0, 16));
            response.append(QString("0x%1 5000").arg(modes, 0, 16));
            response.append(QString("0x%1 6125").arg(modes, 0, 16));
            response.append(QString("0x%1 8333").arg(modes, 0, 16));
            response.append(QString("0x%1 10000").arg(modes, 0, 16));
            response.append(QString("0x%1 12500").arg(modes, 0, 16));
            response.append(QString("0x%1 25000").arg(modes, 0, 16));
            response.append(QString("0x%1 100000").arg(modes, 0, 16));
            response.append(QString("0x%1 250000").arg(modes, 0, 16));
            response.append(QString("0x%1 1000000").arg(modes, 0, 16));
            response.append("0 0");

            modes = getRadioModes("SB");
            if (modes) {
                response.append(QString("0x%1 3000").arg(modes, 0, 16));
                response.append(QString("0x%1 2400").arg(modes, 0, 16));
                response.append(QString("0x%1 1800").arg(modes, 0, 16));
            }
            modes = getRadioModes("AM");
            if (modes) {
                response.append(QString("0x%1 9000").arg(modes, 0, 16));
                response.append(QString("0x%1 6000").arg(modes, 0, 16));
                response.append(QString("0x%1 3000").arg(modes, 0, 16));
            }
            modes = getRadioModes("CW");
            if (modes) {
                response.append(QString("0x%1 1200").arg(modes, 0, 16));
                response.append(QString("0x%1 500").arg(modes, 0, 16));
                response.append(QString("0x%1 200").arg(modes, 0, 16));
            }
            modes = getRadioModes("FM");
            if (modes) {
                response.append(QString("0x%1 15000").arg(modes, 0, 16));
                response.append(QString("0x%1 10000").arg(modes, 0, 16));
                response.append(QString("0x%1 7000").arg(modes, 0, 16));
            }
            modes = getRadioModes("RTTY");
            if (modes) {
                response.append(QString("0x%1 2400").arg(modes, 0, 16));
                response.append(QString("0x%1 500").arg(modes, 0, 16));
                response.append(QString("0x%1 250").arg(modes, 0, 16));
            }
            modes = getRadioModes("PSK");
            if (modes) {
                response.append(QString("0x%1 1200").arg(modes, 0, 16));
                response.append(QString("0x%1 500").arg(modes, 0, 16));
                response.append(QString("0x%1 250").arg(modes, 0, 16));
            }
            response.append("0 0");
            response.append("9900");
            response.append("9900");
            response.append("10000");
            response.append("0");
            QString preamps="";
            if (rigCaps.hasPreamp) {
                for (quint8 pre : rigCaps.preamps)
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
                for (quint8 att : rigCaps.attenuators)
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
            response.append("0xffffffffffffffff");
            response.append("0xffffffffffffffff");
            response.append("0xffffffffffffffff");
            response.append("0xffffffffffffffff");

            if (chkVfoEecuted) {
                response.append(QString("vfo_ops=0x%1").arg(255, 0, 16));
                response.append(QString("ptt_type=0x%1").arg(rigCaps.hasTransmit, 0, 16));
                response.append(QString("has_set_vfo=0x%1").arg(1, 0, 16));
                response.append(QString("has_get_vfo=0x%1").arg(1, 0, 16));
                response.append(QString("has_set_freq=0x%1").arg(1, 0, 16));
                response.append(QString("has_get_freq=0x%1").arg(1, 0, 16));
                response.append(QString("has_set_conf=0x%1").arg(1, 0, 16));
                response.append(QString("has_get_conf=0x%1").arg(1, 0, 16));
                response.append(QString("has_power2mW=0x%1").arg(1, 0, 16));
                response.append(QString("has_mW2power=0x%1").arg(1, 0, 16));
                response.append(QString("timeout=0x%1").arg(1000, 0, 16));
                response.append("done");
            }
        }

        /* Fake command to resolve parsing error */
        else if (command[0] == "fmv") {
            QString resp;

            if (rigState->getChar(CURRENTVFO) == 0) {
                resp.append(QString("%1").arg(rigState->getInt64(VFOAFREQ)));
            }
            else {
                resp.append(QString("%1").arg(rigState->getInt64(VFOBFREQ)));
            }
            response.append(resp);
            resp = "";
            response.append(QString("%1").arg(getMode(rigState->getChar(MODE), rigState->getBool(DATAMODE))));
            response.append(QString("%1").arg(rigState->getUInt16(PASSBAND)));

            if (rigState->getChar(CURRENTVFO) == 0) {
                resp.append("VFOA");
            }
            else {
                resp.append("VFOB");
            }
            response.append(resp);
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
        else if (command.length() > 0 && (command[0] == "2" || command[0] == "power2mW"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 0 && (command[0] == "3" || command[0] == "mW2power"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 0 && (command[0] == "set_clock"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 0 && (command[0] == "get_clock"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command[0] == '\xf0' || command[0] == "chk_vfo")
        {
            chkVfoEecuted = true;
            QString resp;
            if (longReply) {
                resp.append(QString("ChkVFO: "));
            }
            resp.append(QString("%1").arg(rigState->getChar(CURRENTVFO)));
            response.append(resp);
        }
        else if (command.length() > 0 && (command[0] == "set_vfo_opt"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 0 && (command[0] == '\xa2' || command[0] == "set_lock_mode"))
        {
            responseCode = -11; //Unimplemented
        }
        else if (command.length() > 0 && (command[0] == '\xa3' || command[0] == "get_lock_mode"))
        {
            QString resp;
            if (longReply) {
                resp.append(QString("Locked: "));
            }
            resp.append(QString("%1").arg(0)); // Always reply with RIG_OK (0)
            response.append(resp);
        }

        else if (command.length() > 0 && (command[0] == "w" || command[0] == "send_cmd"))
        {
            responseCode = -11; //Unimplemented
        }
        else {
            qInfo(logRigCtlD()) << "Unimplemented command" << commands;
        }
        if (longReply) {
            if (command.length() == 2)
                sendData(QString("%1: %2%3").arg(command[0]).arg(command[1]).arg(sep));
            if (command.length() == 3)
                sendData(QString("%1: %2 %3%4").arg(command[0]).arg(command[1]).arg(command[2]).arg(sep));
            if (command.length() == 4)
                sendData(QString("%1: %2 %3 %4%5").arg(command[0]).arg(command[1]).arg(command[2]).arg(command[3]).arg(sep));
        }

        if (setCommand)
        {
            // This was a set command so state has likely been updated.
            emit parent->stateUpdated();
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

        sep = "\n";
        num = 0;

    }
    commandBuffer.clear();
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
    //qDebug(logRigCtlD()) << sessionId << "TX:" << data;
    if (socket != Q_NULLPTR && socket->isValid() && socket->isOpen())
    {
        socket->write(data.toLatin1());
    }
    else
    {
        qInfo(logRigCtlD()) << "socket not open!";
    }
}

QString rigCtlClient::getFilter(quint8 mode, quint8 filter) {
    
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

QString rigCtlClient::getMode(quint8 mode, bool datamode) {

    QString ret;


    switch (mode) {
    case modeLSB:
        if (datamode) { ret = "PKT"; }
        ret.append("LSB");
        break;
    case modeUSB:
        if (datamode) { ret = "PKT"; }
        ret.append("USB");
        break;
    case modeAM:
        if (datamode) { ret = "PKT"; }
        ret.append("AM");
        break;
    case modeCW:
        ret.append("CW");
        break;
    case modeRTTY:
        ret.append("RTTY");
        break;
    case modeFM:
        if (datamode) { ret = "PKT"; }
        ret.append("FM");
        break;
    case modeWFM:
        ret.append("WFM");
        break;
    case modeCW_R:
        ret.append("CWR");
        break;
    case modeRTTY_R:
        ret.append("RTTYR");
        break;
    case modePSK:
        if (datamode) { ret = "PKT"; }
        ret.append("USB");
        break;
    case modeDV:
        if (datamode) { ret = "PKT"; }
        ret.append("LSB");
        break;
    case 22: // We don't seem to have a mode for this?
        if (datamode) { ret = "PKT"; }
        ret.append("FM");
        break;
    }
    return ret;
}

quint8 rigCtlClient::getMode(QString modeString) {

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


quint8 rigCtlClient::getAntennas()
{
    quint8 ant=0;
    for (quint8 i : rigCaps.antennas)
    {
        ant |= 1<<i;
    }
    return ant;
}

quint64 rigCtlClient::getRadioModes(QString md) 
{
    quint64 modes = 0;
    for (mode_info mode : rigCaps.modes)
    {
        for (int i = 0; mode_str[i].str[0] != '\0'; i++)
        {
            QString mstr = QString(mode_str[i].str);
            if (rigCaps.hasDataModes) {
                if (mstr.contains(mode.name))
                {
                    // qDebug(logRigCtlD()) << "Found data mode:" << mode.name << mode_str[i].mode;
                    if (md.isEmpty() || mstr.contains(md)) {
                        modes |= mode_str[i].mode;
                    }
                }
            }
            else if (mode.name==mstr)
            {
                //qDebug(logRigCtlD()) << "Found mode:" << mode.name << mode_str[i].mode;
                if (md.isEmpty() || mstr==md) 
                {
                    modes |= mode_str[i].mode;
                } 
            }
        }
    }
    return modes;
}

QString rigCtlClient::getAntName(quint8 ant)
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

quint8 rigCtlClient::antFromName(QString name) {
    quint8 ret;

    if (name.toUpper() == "ANT1")
        ret = 0;
    else if (name.toUpper() == "ANT2")
        ret = 1;
    else if (name.toUpper() == "ANT3")
        ret = 2;
    else if (name.toUpper() == "ANT4")
        ret = 3;
    else if (name.toUpper() == "ANT5")
        ret = 4;
    else if (name.toUpper() == "ANT_UNKNOWN")
        ret = 30;
    else if (name.toUpper() == "ANT_CURR")
        ret = 31;
    else  
        ret = 99;
    return ret;
}

quint8 rigCtlClient::vfoFromName(QString vfo) {

    if (vfo.toUpper() == "VFOA" || vfo.toUpper() == "MAIN") return 0;
    if (vfo.toUpper() == "VFOB" || vfo.toUpper() == "SUB") return 1;
    if (vfo.toUpper() == "MEM") return 2;
    return 0;
}

QString rigCtlClient::getVfoName(quint8 vfo)
{
    QString ret;
    switch (vfo) {
    case 0: ret = "VFOA"; break;
    case 1: ret = "VFOB"; break;
    default: ret = "MEM"; break;
    }

    return ret;
}

int rigCtlClient::getCalibratedValue(quint8 meter,cal_table_t cal) {
    
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

unsigned long rigCtlClient::doCrc(unsigned char* p, size_t n)
{
    unsigned long crc = 0xfffffffful;
    size_t i;

    if (crcTable[0] == 0) { genCrc(crcTable); }

    for (i = 0; i < n; i++)
    {
        crc = crcTable[*p++ ^ (crc & 0xff)] ^ (crc >> 8);
    }

    return ((~crc) & 0xffffffff);
}

void rigCtlClient::genCrc(unsigned long crcTable[])
{
    unsigned long POLYNOMIAL = 0xEDB88320;
    unsigned char b = 0;

    while (0 != ++b)
    {
        unsigned long remainder = b;
        unsigned long bit;
        for (bit = 8; bit > 0; --bit)
        {
            if (remainder & 1)
            {
                remainder = (remainder >> 1) ^ POLYNOMIAL;
            }
            else
            {
                remainder = (remainder >> 1);
            }
        }
        crcTable[(size_t)b] = remainder;
    }
}
