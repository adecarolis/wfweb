#include "tciserver.h"

#include "logcategories.h"



// Based on TCI v1.9 commands
static const tciCommandStruct tci_commands[] =
{
    // u=uchar s=short f=float b=bool x=hz m=mode s=string
    { "vfo_limits",         funcNone,       typeFreq,   typeFreq,   typeNone},
    { "if_limits",          funcNone,       typeFreq,   typeFreq,   typeNone},
    { "trx_count",          funcNone,       typeUChar,  typeNone,   typeNone},
    { "channel_count",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "device",             funcNone,       typeShort,  typeNone,   typeNone},
    { "receive_only",       funcNone,       typeBinary, typeNone,   typeNone},
    { "modulations_list",   funcNone,       typeShort,  typeNone,   typeNone},
    { "protocol",           funcNone,       typeShort,  typeNone,   typeNone},
    { "ready",              funcNone,       typeNone,   typeNone,   typeNone},
    { "start",              funcNone,       typeNone,   typeNone,   typeNone},
    { "stop",               funcNone,       typeNone,   typeNone,   typeNone},
    { "dds",                funcNone,       typeUChar,  typeFreq,   typeNone},
    { "if",                 funcNone,       typeUChar,  typeUChar,  typeFreq},
    { "vfo",                funcFreq,   typeUChar,  typeUChar,  typeFreq},
    { "modulation",         funcMode,   typeUChar,  typeMode,   typeNone},
    { "trx",                funcTransceiverStatus,       typeUChar, typeBinary, typeNone},
    { "tune",               funcTunerStatus,typeUChar,  typeNone,   typeNone},
    { "drive",              funcRFPower,    typeUShort,  typeNone,   typeNone},
    { "tune_drive",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "rit_enable",         funcRitStatus,  typeUChar,  typeNone,   typeNone},
    { "xit_enable",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "split_enable",       funcSplitStatus,typeUChar,  typeBinary, typeNone},
    { "rit_offset",         funcRitFreq,    typeUChar,  typeNone,   typeNone},
    { "xit_offset",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_channel_enable",  funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_filter_band",     funcNone,       typeUChar,  typeNone,   typeNone},
    { "cw_macros_speed",    funcNone,       typeUChar,  typeNone,   typeNone},
    { "cw_macros_delay",    funcNone,       typeUChar,  typeNone,   typeNone},
    { "cw_keyer_speed",     funcKeySpeed,   typeUChar,  typeNone,   typeNone},
    { "volume",             funcAfGain,     typeUChar,  typeNone,   typeNone},
    { "mute",               funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_mute",            funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_volume",          funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_balance",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "mon_volume",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "mon_enable",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "agc_mode",           funcNone,       typeUChar,  typeNone,   typeNone},
    { "agc_gain",           funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_nb_enable",       funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_nb_param",        funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_bin_enable",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_nr_enable",       funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_anc_enable",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_anf_enable",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_apf_enable",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_dse_enable",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_nf_enable",       funcNone,       typeUChar,  typeNone,   typeNone},
    { "lock",               funcNone,       typeUChar,  typeNone,   typeNone},
    { "sql_enable",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "sql_level",          funcSquelch,    typeUChar,  typeNone,   typeNone},
    { "tx_enable",          funcNone,       typeUChar,  typeUChar,  typeNone},
    { "cw_macros_speed_up", funcNone,       typeUChar,  typeUChar,  typeNone},
    { "cw_macros_speed_down", funcNone,     typeUChar,  typeUChar,  typeNone},
    { "spot",               funcNone,       typeUChar,  typeUChar,  typeNone},
    { "spot_delete",        funcNone,       typeUChar,  typeUChar,  typeNone},
    { "iq_samplerate",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "audio_samplerate",   funcNone,       typeUChar,  typeNone,   typeNone},
    { "iq_start",           funcNone,       typeUChar,  typeNone,   typeNone},
    { "iq_stop",            funcNone,       typeUChar,  typeNone,   typeNone},
    { "audio_start",        funcNone,       typeUChar,  typeNone,   typeNone},
    { "audio_stop",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "line_out_start",     funcNone,       typeUChar,  typeNone,   typeNone},
    { "line_out_stop",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "line_out_recorder_start", funcNone,  typeUChar,  typeNone,   typeNone},
    { "line_out_recorder_save",  funcNone,  typeUChar,  typeNone,   typeNone},
    { "line_out_recorder_start", funcNone,  typeUChar,  typeNone,   typeNone},
    { "clicked_on_spot",    funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_clicked_on_spot", funcNone,       typeUChar,  typeNone,   typeNone},
    { "tx_footswitch",      funcNone,       typeUChar,  typeBinary,   typeNone},
    { "tx_frequency",       funcNone,       typeUChar,  typeBinary,   typeNone},
    { "app_focus",          funcNone,       typeUChar,  typeBinary,   typeNone},
    { "set_in_focus",       funcNone,       typeUChar,  typeBinary,   typeNone},
    { "keyer",              funcNone,       typeUChar,  typeBinary,   typeNone},
    { "rx_sensors_enable",  funcNone,       typeUChar,  typeBinary,   typeNone},
    { "tx_sensors_enable",  funcNone,       typeUChar,  typeBinary,   typeNone},
    { "rx_sensors",         funcNone,       typeUChar,  typeBinary,   typeNone},
    { "tx_sensors",         funcNone,       typeUChar,  typeBinary,   typeNone},
    { "audio_stream_sample_type", funcNone, typeUChar,  typeBinary,   typeNone},
    { "audio_stream_channels", funcNone,    typeUChar,  typeBinary,   typeNone},
    { "audio_stream_samples", funcNone,     typeUChar,  typeBinary,   typeNone},
    { "digl_offset",        funcNone,       typeUChar,  typeBinary,   typeNone},
    { "digu_offset",        funcNone,       typeUChar,  typeBinary,   typeNone},
    { "rx_smeter",          funcSMeter,     typeUChar,  typeUChar,    typeDouble},
    { 0x0,                   funcNone,       typeNone,   typeNone,     typeNone },
};

#define MAXNAMESIZE 32

tciServer::tciServer(QObject *parent) :
    QObject(parent)
{

}

void tciServer::init(quint16 port) {

    server = new QWebSocketServer(QStringLiteral("TCI Server"), QWebSocketServer::NonSecureMode, this);

    if (server->listen(QHostAddress::Any,port)) {
        qInfo() << "tciServer() listening on" << port;
        connect (server, &QWebSocketServer::newConnection, this, &tciServer::onNewConnection);
        connect (server, &QWebSocketServer::closed, this, &tciServer::socketDisconnected);
    }

    this->setObjectName("TCI Server");
    queue = cachingQueue::getInstance();
    rigCaps = queue->getRigCaps();

    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));
    connect(queue,SIGNAL(cacheUpdated(cacheItem)),this,SLOT(receiveCache(cacheItem)));

    // Setup txChrono packet.
    txChrono.resize(iqHeaderSize);
    dataStream *pStream = reinterpret_cast<dataStream*>(txChrono.data());
    pStream->receiver = 0;
    pStream->sampleRate = 48000;
    pStream->format = IqFloat32;
    pStream->codec = 0u;
    pStream->type = TxChrono;
    pStream->crc = 0u;
    pStream->length = TCI_AUDIO_LENGTH;

}

void tciServer::setupTxPacket(int length)
{
    Q_UNUSED(length)
}

tciServer::~tciServer()
{
    server->close();
    auto it = clients.begin();

    while (it != clients.end())
    {
        it.key()->deleteLater();
        it = clients.erase(it);
    }
}

void tciServer::receiveRigCaps(rigCapabilities *caps)
{
    rigCaps = caps;
}

void tciServer::onNewConnection()
{
    
    QWebSocket *pSocket = server->nextPendingConnection();
    vfoCommandType t = queue->getVfoCommand(vfoA,false,false);

    if (rigCaps == Q_NULLPTR)
    {
        qWarning(logTCIServer()) << "No current rig connection, denying connection request.";
        pSocket->abort();
        return;
    }

    qInfo(logTCIServer()) << "Got rigCaps for" << rigCaps->modelName;

    connect(pSocket, &QWebSocket::textMessageReceived, this, &tciServer::processIncomingTextMessage);
    connect(pSocket, &QWebSocket::binaryMessageReceived, this, &tciServer::processIncomingBinaryMessage);
    connect(pSocket, &QWebSocket::disconnected, this, &tciServer::socketDisconnected);

    qInfo() << "TCI client connected:" << pSocket;
    clients.insert(pSocket,connStatus());
    pSocket->sendTextMessage(QString("protocol:WFVIEW,1.9;"));
    pSocket->sendTextMessage(QString("device:%0;").arg(rigCaps->modelName));
    pSocket->sendTextMessage(QString("receive_only:%0;").arg(rigCaps->hasTransmit?"false":"true"));
    pSocket->sendTextMessage(QString("trx_count:%0;").arg(rigCaps->numReceiver));
    pSocket->sendTextMessage(QString("channels_count:%0;").arg(rigCaps->numVFO));
    quint64 start=UINT64_MAX;
    quint64 end=0;
    for (auto &band: rigCaps->bands)
    {
        if (start > band.lowFreq)
            start = band.lowFreq;
        if (end < band.highFreq)
            end = band.highFreq;
    }
    pSocket->sendTextMessage(QString("vfo_limits:%0,%1;").arg(start).arg(end));
    pSocket->sendTextMessage(QString("if_limits:-19531,19531;"));
    QString mods = "modulations_list:";
    for (modeInfo &mi: rigCaps->modes)
    {

        mods+=mi.name.toUpper();
        mods+=",";
        if (mi.reg == modeUSB || mi.reg == modeLSB)
        {
            mods+=QString("DIG%0").arg(mi.name.at(0));
            mods+=",";
        }
    }
    mods.chop(1);
    mods+=";";
    pSocket->sendTextMessage(mods);
    pSocket->sendTextMessage(QString("vfo:0,0,%0").arg(queue->getCache(t.freqFunc,t.receiver).value.value<freqt>().Hz));
    pSocket->sendTextMessage(QString("modulation:0,%0").arg(tciMode(queue->getCache(t.modeFunc,t.receiver).value.value<modeInfo>())));
    pSocket->sendTextMessage(QString("iq_samplerate:48000;"));
    pSocket->sendTextMessage(QString("audio_samplerate:48000;"));
    pSocket->sendTextMessage(QString("volume:%0;").arg(getValueRange(funcAfGain,-60,0,t.receiver)));
    pSocket->sendTextMessage(QString("mute:%0;").arg(queue->getCache(funcAFMute,t.receiver).value.value<bool>()?"true":"false"));
    pSocket->sendTextMessage(QString("mon_volume:%0;").arg(getValueRange(funcMonitorGain,-60,0,t.receiver)));
    pSocket->sendTextMessage(QString("mon_enable:%0;").arg(queue->getCache(funcMonitor,t.receiver).value.value<bool>()?"false":"frue"));

    pSocket->sendTextMessage(QString("ready;"));
}

int tciServer::getValueRange(funcs func, char min, char max,uchar rx)
{
    int val=min;
    if (rigCaps->commands.contains(func))
    {
        auto cmd = rigCaps->commands.find(func);
        float r = min + (float(queue->getCache(func,rx).value.toInt() - cmd->minVal) * (max - min)) / (float)(cmd->maxVal - cmd->minVal);
        val =  static_cast<int>(r + (r < 0 ? -0.5f : 0.5f));   // round to nearest
    }
    return val;
}

void tciServer::processIncomingTextMessage(QString message)
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());

    auto it = clients.find(pClient);

    if (it == clients.end())
    {
        qInfo() << "Cannot find status for connection" << pClient;
        return;
    }

    it.value().connected = true;

    qDebug() << "TCI Text Message received:" << message;
    QString cmd = message.section(':',0,0);
    QStringList arg = message.section(':',1,1).split(',');
    arg[arg.length()-1].chop(1);
    QString reply = message;

    uchar rx = 0;
    if (arg.size()>0) {
        rx = arg[0].toUInt();
    }
    uchar vfo = 0;
    if (arg.size()>1) {
        vfo = arg[1].toUInt();
    }
    bool set = false;

    for (int i=0; tci_commands[i].str != 0x00; i++)
    {
        cmd=cmd.toLower();
        if (!strncmp(cmd.toLocal8Bit(), tci_commands[i].str,MAXNAMESIZE))
        {
            tciCommandStruct tc = tci_commands[i];
            uchar numArgs=0;
            if (tc.arg1 != typeNone)
                numArgs++;
            if (tc.arg2 != typeNone)
                numArgs++;
            if (tc.arg3 != typeNone)
                numArgs++;

            // Some clients seem to send additional text that's unexpected!
            if (numArgs <= arg.count())
            {
                set = true;
                qInfo() << "This is a set command";
            }

            qDebug() << "Found command:" << tc.str;
            if (vfo && rigCaps->numVFO <= vfo && rigCaps->numReceiver > 1)
            {
                // invalid VFO swap to receiver instead.
                rx = vfo;
                vfo = 0;
            }
            vfoCommandType t = queue->getVfoCommand(vfo_t(vfo),rx,set);
            funcs func = tc.func;

            if (cmd == "rx_mute" )
            {
            }
            else if (cmd == "audio_samplerate" )
            {
            }
            else if (set)
            {
                QVariant val;
                if (arg.count() == 1 && tc.arg1 == typeUChar)
                {
                    val = QVariant::fromValue(uchar(arg[0].toInt(NULL)));
                }

                if (arg.count() == 1 && tc.arg1 == typeUShort)
                {
                    val = QVariant::fromValue(ushort(arg[0].toInt(NULL)*2.55));
                }

                if (arg.count() == 2 && tc.arg2 == typeUChar)
                {
                    val = QVariant::fromValue(uchar(arg[1].toInt(NULL)));
                }

                if (arg.count() == 2 && tc.arg2 == typeUShort)
                {
                    val = QVariant::fromValue(ushort(arg[1].toInt(NULL)*2.55));
                }
                else if (arg.count() == 3 && tc.arg3 == typeFreq)
                {
                    val=QVariant::fromValue<freqt>(freqt(arg[2].toUInt(),arg[2].toUInt() / (double)1E6,selVFO_t::activeVFO));
                    func = t.freqFunc;
                }
                else if (tc.arg2 == typeMode)
                {
                    val = QVariant::fromValue<modeInfo>(rigMode(arg[1]));
                    func = t.modeFunc;
                }
                else if (tc.arg2 == typeBinary)
                {
                    val = QVariant::fromValue(arg[1]=="true"?true:false);
                }

                if (func != funcNone && val.isValid()) {
                    queue->add(priorityImmediate,queueItem(func,val,false,rx));
                    //if (func != funcMode)
                    //continue;
                }
            }

            if (cmd == "audio_start" )
            {
                qInfo() << "Starting audio";
                it.value().rxaudio=true;
                reply = QString("audio_start:%0").arg(rx);
            }
            else if (cmd == "audio_stop" )
            {
                it.value().rxaudio=false;
                qInfo() << "Stopping audio";
                reply = QString("audio_stop:%0").arg(rx);
            }

            if (tc.func != funcNone)
            {
                reply = QString("%0").arg(cmd);
                QVariant v = queue->getCache(func,t.receiver).value;
                if (!v.isValid())
                {
                    qWarning() << "Requested cache item contains no data" << funcString[func] << "on rx" << t.receiver;
                    continue;
                } else {
                    if (tc.arg3 == typeFreq) {
                        v = queue->getCache(t.freqFunc,t.receiver).value;
                    }
                    if (tc.arg3 == typeMode) {
                        v = queue->getCache(t.modeFunc,t.receiver).value;
                    }
                }

                if (tc.arg1 == typeUChar && numArgs > 1)
                {
                    // Multi arg replies always contain receiver number
                    reply += QString(":%0").arg(arg[0]);
                }

                if (numArgs == 1 && tc.arg1 == typeUChar)
                {
                    reply += QString(":%0").arg(v.value<uchar>());
                }
                else if (numArgs ==1 && tc.arg1 == typeUShort)
                {
                    reply += QString(":%0").arg(round(v.value<ushort>()/2.55));
                }
                else if (numArgs == 2 && tc.arg2 == typeUChar)
                {

                    reply += QString(",%0").arg(v.value<uchar>());
                }
                else if (numArgs == 2 && tc.arg2 == typeUShort)
                {
                    reply += QString(",%0").arg(round(v.value<ushort>()/2.55));
                }
                else if (numArgs == 3 &&  tc.arg3 == typeFreq)
                {
                    reply += QString(",%0,%1").arg(arg[1].toInt(NULL)).arg(v.value<freqt>().Hz);
                }
                else if (numArgs == 3 && tc.arg3 == typeDouble)
                {
                    reply += QString(",%0,%1").arg(vfo).arg(v.value<double>()-dBmConversion,0,'f',0);
                }

                else if (tc.arg2 == typeMode)
                {
                    reply += QString(",%0").arg(tciMode(v.value<modeInfo>()));
                }
                else if (tc.arg2 == typeBinary)
                {
                    reply += QString(",%0").arg(v.value<bool>()?"true":"false");
                }
                else
                {
                    // Nothing to say!
                    qInfo() << "Nothing to say:" << reply;
                    return;
                }
                reply += ";";
            }
            else
            {
                reply = message; // Reply with the sent message
            }

            if (pClient) {
                qDebug() << "Reply:" << reply;
                pClient->sendTextMessage(reply);
            }
        }
    }
}

void tciServer::processIncomingBinaryMessage(QByteArray message)
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient) {
        dataStream *pStream = reinterpret_cast<dataStream*>(message.data());
        if (pStream->type == TxAudioStream && pStream->length > 0)
        {
            QByteArray tempData(pStream->length * sizeof(float),0x0);
            memcpy(tempData.data(),pStream->data,tempData.size());
            //qInfo() << QString("Received audio from client: %0 Sample: %1 Format: %2 Length: %3(%4 bytes) Type: %5").arg(pStream->receiver).arg(pStream->sampleRate).arg(pStream->format).arg(pStream->length).arg(tempData.size()).arg(pStream->type);
            emit sendTCIAudio(tempData);
        }
    }
}

void tciServer::socketDisconnected()
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    qInfo() << "TCI client disconnected:" << pClient;
    if (pClient) {
        clients.remove(pClient);
        pClient->deleteLater();
    }
}

void tciServer::receiveTCIAudio(audioPacket audio){
    /*
     *      quint32 receiver; //!< receiver’s periodic number
            quint32 sampleRate; //!< sample rate
            quint32 format; //!< always equals 4 (float 32 bit)
            quint32 codec; //!< compression algorithm (not implemented yet), always 0
            quint32 crc; //!< check sum (not implemented yet), always 0
            quint32 length; //!< length of data field
            quint32 type; //!< type of data stream
            quint32 reserv[9]; //!< reserved
            float data[4096]; //!< data field
    */

    //dataStream *pStream = reinterpret_cast<dataStream*>(audio.data());

    rxAudioData.resize(iqHeaderSize + audio.data.size());
    dataStream *pStream = reinterpret_cast<dataStream*>(rxAudioData.data());
    pStream->receiver = 0;
    pStream->sampleRate = 48000;
    pStream->format = IqFloat32;
    pStream->codec = 0u;
    pStream->type = RxAudioStream;
    pStream->crc = 0u;
    pStream->length = quint32(audio.data.size() / sizeof (float));

    memcpy(pStream->data,audio.data.data(),audio.data.size());

    auto it = clients.begin();
    while (it != clients.end())
    {
        if (it.value().connected && it.value().rxaudio)
        {
            //qInfo() << "Sending audio to client:" << it.key() << "size" << audio.data.size();
            it.key()->sendBinaryMessage(txChrono);
            it.key()->sendBinaryMessage(rxAudioData);

        }
        ++it;
    }
}


void tciServer::receiveCache(cacheItem item)
{
    auto it = clients.begin();
    QString reply;

    // Special case for different commands
    funcs func = item.command;
    uchar vfo = 0;
    if (func == funcModeTR || func == funcSelectedMode)
    {
        func = funcMode;
    }
    else if (func == funcFreqTR || func == funcSelectedFreq)
    {
        func = funcFreq;
    }
    else if (func == funcUnselectedMode)
    {
        func = funcMode;
        vfo = 1;
    }
    else if (func == funcUnselectedFreq)
    {
        func = funcFreq;
        vfo = 1;
    }

    while (it != clients.end())
    {
        if (it.value().connected)
        {
            for (int i=0; tci_commands[i].str != 0x00; i++)
            {
                if (func == tci_commands[i].func)
                {
                    // Found a matching command, send a reply based on param type
                    tciCommandStruct tc = tci_commands[i];
                    // Here we can process the arguments

                    uchar numArgs=0;
                    if (tc.arg1 != typeNone)
                        numArgs++;
                    if (tc.arg2 != typeNone)
                        numArgs++;
                    if (tc.arg3 != typeNone)
                        numArgs++;

                    if (tc.func != funcNone)
                    {
                        if (tc.arg1 == typeUChar && numArgs > 1)
                            // Multi arg replies always contain receiver number
                            reply = QString("%0:%1").arg(tc.str).arg(QString::number(item.receiver));
                            // Single arg replies don't!
                        else if (numArgs == 1 && tc.arg1 == typeUChar)
                            reply = QString("%0:%1").arg(tc.str).arg(item.value.value<uchar>());
                        else if (numArgs == 1 && tc.arg1 == typeUShort)
                            reply = QString("%0:%1").arg(tc.str).arg(round(item.value.value<ushort>()/2.55));

                        if (numArgs == 2 && tc.arg2 == typeUChar)
                        {
                            reply += QString(",%0").arg(item.value.value<uchar>());
                        }
                        else if (numArgs == 2 && tc.arg2 == typeUShort)
                        {
                            reply += QString(",%0").arg(round(item.value.value<ushort>()/2.55));
                        }
                        else if (numArgs == 3 && tc.arg3 == typeDouble)
                        {
                            reply += QString(",%0,%1").arg(vfo).arg(item.value.value<double>()-dBmConversion,0,'f',0);
                        }
                        else if (numArgs == 3 && tc.arg3 == typeFreq)
                        {


                            if (rigCaps->numReceiver > 1 && item.receiver > 0)
                            {
                                vfo = 1;
                            }

                            reply += QString(",%0,%1").arg(vfo).arg(quint64(item.value.value<freqt>().Hz));
                            // Slight hack to correctly convert the relative dB to dBm
                            if (item.value.value<freqt>().Hz > 30000000)
                            {
                                dBmConversion = 93;
                            } else
                            {
                                dBmConversion = 73;
                            }
                        }
                        else if (tc.arg2 == typeMode)
                        {
                            reply += QString(",%0").arg(tciMode(item.value.value<modeInfo>()));
                        }
                        else if (tc.arg2 == typeBinary)
                        {
                            reply += QString(",%0").arg(item.value.value<bool>()?"true":"false");
                        }
                        reply += ";";
                        it.key()->sendTextMessage(reply);
                        //qInfo() << "Sending Cache TCI:" << reply;
                    }
                }
            }

        }
        ++it;
    }
}


QString tciServer::tciMode(modeInfo m)
{
    QString ret="";
    if (m.mk == modeUSB && m.data && m.data != 0xff)
        ret="digu";
    else if (m.mk == modeLSB && m.data != 0xff)
        ret="digl";
    else if (m.mk == modeFM)
        ret="nfm";
    else
        ret = m.name.toLower();
    return ret;
}

modeInfo tciServer::rigMode(QString mode)
{
    modeInfo m;
    m.mk=modeUnknown;
    qInfo() << "Searching for mode" << mode;
    for (modeInfo &mi: rigCaps->modes)
    {
        if (mode.toLower() =="digl" && mi.mk == modeLSB)
        {
            m = modeInfo(mi);
            m.data = true;
            break;
        }
        else if (mode.toLower() == "digu" && mi.mk == modeUSB)
        {
            m = modeInfo(mi);
            m.data = true;
            break;
        } else if ((mode.toLower() == "nfm" && mi.mk == modeFM) || (mode.toLower() == mi.name.toLower()))
        {
            m = modeInfo(mi);
            m.data = false;
            break;
        }
    }
    m.filter = 1;
    qInfo(logTCIServer()) << "Got Mode:" << m.name << "data:" << m.data;
    return m;
}
