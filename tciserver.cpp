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
    { "vfo",                funcMainFreq,   typeUChar,  typeUChar,  typeFreq},
    { "modulation",         funcMainMode,   typeUChar,  typeMode,   typeNone},
    { "trx",                funcTransceiverStatus,       typeUChar, typeBinary, typeNone},
    { "tune",               funcTunerStatus,typeUChar,  typeNone,   typeNone},
    { "drive",              funcRFPower,    typeUShort,  typeNone,   typeNone},
    { "tune_drive",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "rit_enable",         funcRitStatus,  typeUChar,  typeNone,   typeNone},
    { "xit_enable",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "split_enable",       funcSplitStatus,typeUChar,  typeBinary, typeNone},
    { "rit_offset",         funcRITFreq,    typeUChar,  typeNone,   typeNone},
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
    { "rx_smeter",          funcSMeter,     typeUChar,  typeUChar,    typedB},
    { "",                   funcNone,       typeNone,   typeNone,     typeNone },
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
    queue = cachingQueue::getInstance(this);
    rigCaps = queue->getRigCaps();

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
    pSocket->sendTextMessage(QString("protocol:WFVIEW,1.5;\n"));
    pSocket->sendTextMessage(QString("device:%0;\n").arg(rigCaps->modelName));
    pSocket->sendTextMessage(QString("receive_only:%0;\n").arg(rigCaps->hasTransmit?"false":"true"));
    pSocket->sendTextMessage(QString("trx_count:1;\n"));
    pSocket->sendTextMessage(QString("channel_count:1;\n"));
    quint64 start=UINT64_MAX;
    quint64 end=0;
    for (auto &band: rigCaps->bands)
    {
        if (start > band.lowFreq)
            start = band.lowFreq;
        if (end < band.highFreq)
            end = band.highFreq;
    }
    pSocket->sendTextMessage(QString("vfo_limits:%0,%1;\n").arg(start).arg(end));
    pSocket->sendTextMessage(QString("if_limits:-48000,48000;\n"));
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
    mods+=";\n";
    pSocket->sendTextMessage(mods);
    // pSocket->sendTextMessage(QString("modulations_list:AM,LSB,USB,CW,NFM,WSPR,FT8,FT4,JT65,JT9,RTTY,BPSK,DIGL,DIGU,WFM,DRM;\n"));
    pSocket->sendTextMessage(QString("iq_samplerate:48000;\n"));
    pSocket->sendTextMessage(QString("audio_samplerate:48000;\n"));
    pSocket->sendTextMessage(QString("mute:false;\n"));
    pSocket->sendTextMessage(QString("vfo:0,0,%0;").arg(queue->getCache(funcMainFreq,false).value.value<freqt>().Hz));
    pSocket->sendTextMessage(QString("modulation:0,%0;").arg(tciMode(queue->getCache(funcMainMode,false).value.value<modeInfo>())));
    pSocket->sendTextMessage(QString("start;\n"));
    pSocket->sendTextMessage(QString("ready;\n"));
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

    qInfo() << "TCI Text Message received:" << message;
    QString cmd = message.section(':',0,0);
    QStringList arg = message.section(':',1,1).split(',');
    arg[arg.length()-1].chop(1);
    QString reply = message;
    uchar sub = arg[0].toUInt();
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
                    val = QVariant::fromValue(uchar(arg[0].toInt(NULL)));
                if (arg.count() == 1 && tc.arg1 == typeUShort)
                    val = QVariant::fromValue(ushort(arg[0].toInt(NULL)*2.55));
                if (arg.count() == 2 && tc.arg2 == typeUChar)
                    val = QVariant::fromValue(uchar(arg[1].toInt(NULL)));
                if (arg.count() == 2 && tc.arg2 == typeUShort)
                    val = QVariant::fromValue(ushort(arg[1].toInt(NULL)*2.55));
                else if (arg.count() == 3 && tc.arg3 == typeFreq)
                {
                    freqt f;
                    f.Hz = arg[2].toUInt();
                    f.MHzDouble = f.Hz / (double)1E6;
                    val=QVariant::fromValue(f);
                }
                else if (tc.arg2 == typeMode)
                    val = QVariant::fromValue(rigMode(arg[1]));
                else if (tc.arg2 == typeBinary)
                    val = QVariant::fromValue(arg[1]=="true"?true:false);

                if (tc.func != funcNone && val.isValid()) {
                    queue->add(priorityImmediate,queueItem(tc.func,val,false,sub));
                    if (tc.func != funcMainMode)
                        continue;
                }
            }

            if (cmd == "audio_start" )
            {
                qInfo() << "Starting audio";
                it.value().rxaudio=true;
            }
            else if (cmd == "audio_stop" )
            {
                it.value().rxaudio=false;
                qInfo() << "Stopping audio";
            }

            if (tc.func != funcNone)
            {
                reply = QString("%0").arg(cmd);

                if (tc.arg1 == typeUChar && numArgs > 1)
                    // Multi arg replies always contain receiver number
                    reply += QString(":%0").arg(sub);

                if (numArgs == 1 && tc.arg1 == typeUChar)

                    reply += QString(":%0").arg(queue->getCache(tc.func,sub).value.value<uchar>());

                else if (numArgs ==1 && tc.arg1 == typeUShort)

                    reply += QString(":%0").arg(round(queue->getCache(tc.func,sub).value.value<ushort>()/2.55));

                else if (numArgs == 2 && tc.arg2 == typeUChar)

                    reply += QString(",%0").arg(queue->getCache(tc.func,sub).value.value<uchar>());

                else if (numArgs == 2 && tc.arg2 == typeUShort)

                    reply += QString(",%0").arg(round(queue->getCache(tc.func,sub).value.value<ushort>()/2.55));

                else if (numArgs == 3 &&  tc.arg3 == typeFreq &&
                         (!set || queue->getCache(tc.func,sub).value.value<freqt>().Hz == arg[2].toULongLong()))

                    reply += QString(",%0,%1").arg(arg[1].toInt(NULL)).arg(quint64(queue->getCache(tc.func,sub).value.value<freqt>().Hz));

                else if (tc.arg2 == typeMode && (!set ||
                         tciMode(queue->getCache(tc.func,sub).value.value<modeInfo>()) == arg[1].toLower()))

                    reply += QString(",%0").arg(tciMode(queue->getCache(tc.func,sub).value.value<modeInfo>()));

                else if (tc.arg2 == typeBinary)

                    reply += QString(",%0").arg(queue->getCache(tc.func,sub).value.value<bool>()?"true":"false");

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
                qInfo() << "Reply:" << reply;
                pClient->sendTextMessage(reply);
            }
        }
    }

    /*
    if (cmd.toLower() == "modulation")
    {
        reply = QString("%0:%1,%2;").arg(cmd).arg(sub)
                    .arg(queue->getCache(sub?funcSubFreq:funcMainMode,sub).value.value<modeInfo>().name);
    } else if (cmd == "rx_enable" || cmd == "tx_enable") {
        reply = QString("%0:%1,%2;").arg(cmd).arg(sub).arg("true");
    }
    else if (cmd == "audio_start" ) {
        it.value().rxaudio=true;
    }
    else if (cmd == "audio_stop" ) {
        it.value().rxaudio=false;
    }
    else if (cmd == "rx_mute" ) {
    }
    else if (cmd == "drive" ) {
        reply = QString("%0:%1,%2;").arg(cmd).arg(0).arg(50);
    }
    else if (cmd == "audio_samplerate" ) {
    }
    else if (cmd == "trx" ) {
        if (arg.size() == 1) {
            it.value().rxaudio=false;
            reply = QString("trx:%0,%1;").arg(
                QString::number(sub),queue->getCache(funcTransceiverStatus).value.value<bool>()?"true":"false");
        }
        else if (arg.size() > 1) {
            bool on = arg[1]=="true"?true:false;
            queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue(on),false,sub));
        }
    }
    else if (cmd == "vfo")
    {
        if (arg.size() == 2) {
            reply = QString("%0:%1,%2,%3;").arg(cmd,arg[0],arg[1])
                        .arg(queue->getCache(sub?funcSubFreq:funcMainFreq,sub).value.value<freqt>().Hz);
        }
        else if (arg.size() == 3) {
            qInfo() << "Freq" << arg[2];
            freqt f;
            f.Hz = arg[2].toUInt();
            f.MHzDouble = f.Hz / (double)1E6;
            queue->add(priorityImmediate,queueItem(sub?funcSubFreq:funcMainFreq,QVariant::fromValue(f),false,sub));
        }
    }
    else if (cmd == "modulation")
    {
        if (arg.size() == 1) {
            reply = QString("modulation:%0,%1;").arg(
                        QString::number(sub),queue->getCache(sub?funcSubMode:funcMainMode,sub).value.value<modeInfo>().name.toLower());
        }
        else if (arg.size() == 2) {
            qInfo() << "Mode (TODO)" << arg[1];
            reply = QString("modulation:%0,%1;").arg(
                        QString::number(sub),queue->getCache(sub?funcSubMode:funcMainMode,sub).value.value<modeInfo>().name.toLower());
        }
    }
    //reply = QString("vfo:0,%0,%1;").arg(QString::number(item.sub)).arg(item.value.value<freqt>().Hz);

    */

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
    if (func == funcModeTR || func == funcSelectedMode || func == funcSubMode)
    {
        func = funcMainMode;
    }
    else if (func == funcFreqTR || func == funcSelectedFreq || func == funcSubFreq)
    {
        func = funcMainFreq;
    }
    else if (func == funcUnselectedMode)
    {
        func = funcMainMode;
        vfo = 1;
    }
    else if (func == funcUnselectedFreq)
    {
        func = funcMainFreq;
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


                        if (numArgs == 2 && tc.arg2 == typeUChar) {
                            reply += QString(",%0").arg(item.value.value<uchar>());
                        } else if (numArgs == 2 && tc.arg2 == typeUShort) {
                            reply += QString(",%0").arg(round(item.value.value<ushort>()/2.55));
                        } else if (numArgs == 3 && tc.arg3 == typedB) {
                            int val = item.value.value<uchar>();
                            val = (val/2.42) - 120; // Approximate s-meter.
                            reply += QString(",%0,%1").arg(vfo).arg(val);
                        } else if (numArgs == 3 && tc.arg3 == typeFreq) {
                            reply += QString(",%0,%1").arg(vfo).arg(quint64(item.value.value<freqt>().Hz));
                        } else if (tc.arg2 == typeMode) {
                            reply += QString(",%0").arg(tciMode(item.value.value<modeInfo>()));
                        } else if (tc.arg2 == typeBinary) {
                            reply += QString(",%0").arg(item.value.value<bool>()?"true":"false");
                        }
                        reply += ";";
                        it.key()->sendTextMessage(reply);
                        //qInfo() << "Sending TCI:" << reply;
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
    if (m.mk == modeUSB && m.data > 0)
        ret="digu";
    else if (m.mk == modeLSB && m.data > 0)
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
    qInfo() << "Searching for mode" << mode;
    for (modeInfo &mi: rigCaps->modes)
    {
        if (mode.toLower() =="digl" && mi.mk == modeLSB)
        {
            m = modeInfo(mi);
            m.data = true;
            m.filter = 1;
            break;
        } else if (mode.toLower() =="digu" && mi.mk == modeUSB)
        {
            m = modeInfo(mi);
            m.data = true;
            break;
        } else if (mode.toLower() == "nfm" && mi.mk == modeFM)
        {
            m = modeInfo(mi);
            break;
        } else if (mode.toLower() == mi.name.toLower())
        {
            m = modeInfo(mi);
            break;
        }
    }
    m.filter = 1;
    return m;
}
