#include "tciserver.h"

#include "logcategories.h"



// Based on TCI v1.9 commands
static const tciCommandStruct tci_commands[] =
{
    // u=uchar s=short f=float b=bool x=hz m=mode s=string
    { "vfo_limits",         funcNone,       typeFreq, typeFreq},
    { "if_limits",          funcNone,       typeFreq, typeFreq},
    { "trx_count",          funcNone,       typeUChar},
    { "channel_count",      funcNone,       typeUChar},
    { "device",             funcNone,       typeShort},
    { "receive_only",       funcNone,       typeBinary},
    { "modulations_list",   funcNone,       typeShort},
    { "protocol",           funcNone,       typeShort},
    { "ready",              funcNone },
    { "start",              funcNone },
    { "stop",               funcNone },
    { "dds",                funcNone, typeUChar,typeFreq},
    { "if",                 funcNone,       typeUChar,typeUChar,typeFreq},
    { "vfo",                funcNone,       typeUChar,typeUChar,typeFreq},
    { "modulation",         funcMainMode,  typeUChar,typeShort},
    { "trx",                funcTransceiverStatus,       typeUChar},
    { "tune",               funcTunerStatus,       typeUChar},
    { "drive",              funcNone,       typeUChar},
    { "tune_drive",         funcNone,       typeUChar},
    { "rit_enable",         funcRitStatus,       typeUChar},
    { "xit_enable",         funcNone,       typeUChar},
    { "split_enable",       funcSplitStatus,       typeUChar},
    { "rit_offset",         funcRITFreq,       typeUChar},
    { "xit_offset",         funcNone,       typeUChar},
    { "rx_channel_enable",  funcNone,       typeUChar},
    { "rx_filter_band",     funcNone,       typeUChar},
    { "cw_macros_speed",    funcNone,       typeUChar},
    { "cw_macros_delay",    funcNone,       typeUChar},
    { "cw_keyer_speed",     funcKeySpeed,       typeUChar},
    { "volume",             funcNone,       typeUChar},
    { "mute",               funcNone,       typeUChar},
    { "rx_mute",            funcNone,       typeUChar},
    { "rx_volume",          funcNone,       typeUChar},
    { "rx_balance",         funcNone,       typeUChar},
    { "mon_volume",         funcNone,       typeUChar},
    { "mon_enable",         funcNone,       typeUChar},
    { "agc_mode",           funcNone,       typeUChar},
    { "agc_gain",           funcNone,       typeUChar},
    { "rx_nb_enable",       funcNone,       typeUChar},
    { "rx_nb_param",        funcNone,       typeUChar},
    { "rx_bin_enable",      funcNone,       typeUChar},
    { "rx_nr_enable",       funcNone,       typeUChar},
    { "rx_anc_enable",      funcNone,       typeUChar},
    { "rx_anf_enable",      funcNone,       typeUChar},
    { "rx_apf_enable",      funcNone,       typeUChar},
    { "rx_dse_enable",      funcNone,       typeUChar},
    { "rx_nf_enable",       funcNone,       typeUChar},
    { "lock",               funcNone,       typeUChar},
    { "sql_enable",         funcNone,       typeUChar},
    { "sql_level",          funcSquelch,       typeUChar},
    { "tx_enable",          funcNone,       typeUChar,typeUChar},
    { "cw_macros_speed_up", funcNone,       typeUChar,typeUChar},
    { "cw_macros_speed_down", funcNone,       typeUChar,typeUChar},
    { "spot",               funcNone,       typeUChar,typeUChar},
    { "spot_delete",        funcNone,       typeUChar,typeUChar},
    { "iq_samplerate",      funcNone,       typeUChar},
    { "audio_samplerate",   funcNone,       typeUChar},
    { "iq_start",           funcNone,       typeUChar},
    { "iq_stop",            funcNone,       typeUChar},
    { "audio_start",        funcNone,       typeUChar},
    { "audio_stop",         funcNone,       typeUChar},
    { "line_out_start",     funcNone,       typeUChar},
    { "line_out_stop",      funcNone,       typeUChar},
    { "line_out_recorder_start", funcNone,       typeUChar},
    { "line_out_recorder_save",  funcNone,       typeUChar},
    { "line_out_recorder_start", funcNone,       typeUChar},
    { "clicked_on_spot",    funcNone,       typeUChar},
    { "rx_clicked_on_spot", funcNone,       typeUChar},
    { "tx_footswitch",      funcNone,       typeUChar,typeBinary},
    { "tx_frequency",       funcNone,       typeUChar,typeBinary},
    { "app_focus",          funcNone,       typeUChar,typeBinary},
    { "set_in_focus",       funcNone,       typeUChar,typeBinary},
    { "keyer",              funcNone,       typeUChar,typeBinary},
    { "rx_sensors_enable",  funcNone,       typeUChar,typeBinary},
    { "tx_sensors_enable",  funcNone,       typeUChar,typeBinary},
    { "rx_sensors",         funcNone,       typeUChar,typeBinary},
    { "tx_sensors",         funcNone,       typeUChar,typeBinary},
    { "audio_stream_sample_type", funcNone,       typeUChar,typeBinary},
    { "audio_stream_channels", funcNone,       typeUChar,typeBinary},
    { "audio_stream_samples", funcNone,       typeUChar,typeBinary},
    { "digl_offset",        funcNone,       typeUChar,typeBinary},
    { "digu_offset",        funcNone,       typeUChar,typeBinary},
    { "", funcNone, typeNone },
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
    pSocket->sendTextMessage(QString("device:WFVIEW;\n"));
    pSocket->sendTextMessage(QString("receive_only:%0;\n").arg(rigCaps->hasTransmit?"false":"true"));
    pSocket->sendTextMessage(QString("trx_count:1;\n"));
    pSocket->sendTextMessage(QString("channel_count:1;\n"));
    pSocket->sendTextMessage(QString("vfo_limits:10000,52000000;\n"));
    pSocket->sendTextMessage(QString("if_limits:-48000,48000;\n"));
    pSocket->sendTextMessage(QString("modulations_list:AM,LSB,USB,CW,NFM,WSPR,FT8,FT4,JT65,JT9,RTTY,BPSK,DIGL,DIGU,WFM,DRM;\n"));
    pSocket->sendTextMessage(QString("iq_samplerate:48000;\n"));
    pSocket->sendTextMessage(QString("audio_samplerate:48000;\n"));
    pSocket->sendTextMessage(QString("mute:false;\n"));
    pSocket->sendTextMessage(QString("vfo:0,0,%0;").arg(queue->getCache(funcMainFreq,false).value.value<freqt>().Hz));
    pSocket->sendTextMessage(QString("modulation:0,%0;").arg(queue->getCache(funcMainMode,false).value.value<modeInfo>().name.toLower()));
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

    /*
    for (int i=0; tci_commands[i].str != 0x00; i++)
    {
        if (!strncmp(cmd.toLocal8Bit(), tci_commands[i].str,MAXNAMESIZE))
        {
            qDebug() << "Found command:" << cmd;
        }
    }
    */

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
            /*
            freqt f;
            f.Hz = arg[2].toUInt();
            f.MHzDouble = f.Hz / (double)1E6;
            queue->add(priorityImmediate,queueItem(sub?funcSubFreq:funcMainFreq,QVariant::fromValue(f),false,sub));
            */
        }
    }
    //reply = QString("vfo:0,%0,%1;").arg(QString::number(item.sub)).arg(item.value.value<freqt>().Hz);


    if (pClient && !reply.isEmpty()) {
        qInfo() << "Reply:" << reply;
        pClient->sendTextMessage(reply);
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
    while (it != clients.end())
    {
        if (it.value().connected)
        {

            switch (item.command)
            {
            case funcFreqTR:
            case funcMainFreq:
            case funcSubFreq:
                reply = QString("vfo:0,%0,%1;").arg(QString::number(item.receiver),item.value.value<freqt>().Hz);
                break;
            case funcModeTR:
            case funcMainMode:
            case funcSubMode:
                reply = QString("modulation:%0,%1;").arg(QString::number(item.receiver),item.value.value<modeInfo>().name.toLower());
                break;
            case funcTransceiverStatus:
                reply = QString("trx:%0,%1;").arg(QString::number(item.receiver),item.value.value<bool>()?"true":"false");
                break;
            default:
                break;
            }
            if (!reply.isEmpty()) {
                it.key()->sendTextMessage(reply);
                qInfo() << "Sending TCI:" << reply;
            }

        }
        ++it;
    }

}
