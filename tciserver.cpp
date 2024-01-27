#include "tciserver.h"

#include "logcategories.h"



// Based on TCI v1.9 commands
static const tciCommandStruct tci_commands[] =
{
    // u=uchar s=short f=float b=bool x=hz m=mode s=string
    { "vfo_limits",         funcNone,       'x', 'x'},
    { "if_limits",          funcNone,       'x', 'x'},
    { "trx_count",          funcNone,       'u'},
    { "channel_count",      funcNone,       'u'},
    { "device",             funcNone,       's'},
    { "receive_only",       funcNone,       'b'},
    { "modulations_list",   funcNone,       's'},
    { "protocol",           funcNone,       's'},
    { "ready",              funcNone },
    { "start",              funcNone },
    { "stop",               funcNone },
    { "dds",                funcNone, 'u','x'},
    { "if",          funcNone,       'u','u','x'},
    { "vfo",          funcNone,       'u','u','x'},
    { "modulation",          funcNone,  'u','s'},
    { "trx",          funcNone,       'u'},
    { "tune",          funcNone,       'u'},
    { "drive",          funcNone,       'u'},
    { "tune_drive",          funcNone,       'u'},
    { "rit_enable",          funcNone,       'u'},
    { "xit_enable",          funcNone,       'u'},
    { "split_enable",          funcNone,       'u'},
    { "rit_offset",          funcNone,       'u'},
    { "xit_offset",          funcNone,       'u'},
    { "rx_channel_enable",          funcNone,       'u'},
    { "rx_filter_band",          funcNone,       'u'},
    { "cw_macros_speed",          funcNone,       'u'},
    { "cw_macros_delay",          funcNone,       'u'},
    { "cw_keyer_speed",          funcNone,       'u'},
    { "volume",          funcNone,       'u'},
    { "mute",          funcNone,       'u'},
    { "rx_mute",          funcNone,       'u'},
    { "rx_volume",          funcNone,       'u'},
    { "rx_balance",          funcNone,       'u'},
    { "mon_volume",          funcNone,       'u'},
    { "mon_enable",          funcNone,       'u'},
    { "agc_mode",          funcNone,       'u'},
    { "agc_gain",          funcNone,       'u'},
    { "rx_nb_enable",          funcNone,       'u'},
    { "rx_nb_param",          funcNone,       'u'},
    { "rx_bin_enable",          funcNone,       'u'},
    { "rx_nr_enable",          funcNone,       'u'},
    { "rx_anc_enable",          funcNone,       'u'},
    { "rx_anf_enable",          funcNone,       'u'},
    { "rx_apf_enable",          funcNone,       'u'},
    { "rx_dse_enable",          funcNone,       'u'},
    { "rx_nf_enable",          funcNone,       'u'},
    { "lock",          funcNone,       'u'},
    { "sql_enable",          funcNone,       'u'},
    { "sql_level",          funcNone,       'u'},
    { "tx_enable",          funcNone,       'u','u'},
    { "cw_macros_speed_up",          funcNone,       'u','u'},
    { "cw_macros_speed_down",          funcNone,       'u','u'},
    { "spot",          funcNone,       'u','u'},
    { "spot_delete",          funcNone,       'u','u'},
    { "iq_samplerate",          funcNone,       'u'},
    { "audio_samplerate",          funcNone,       'u'},
    { "iq_start",          funcNone,       'u'},
    { "iq_stop",          funcNone,       'u'},
    { "audio_start",          funcNone,       'u'},
    { "audio_stop",          funcNone,       'u'},
    { "line_out_start",          funcNone,       'u'},
    { "line_out_stop",          funcNone,       'u'},
    { "line_out_recorder_start",          funcNone,       'u'},
    { "line_out_recorder_save",          funcNone,       'u'},
    { "line_out_recorder_start",          funcNone,       'u'},
    { "clicked_on_spot",          funcNone,       'u'},
    { "rx_clicked_on_spot",          funcNone,       'u'},
    { "tx_footswitch",      funcNone,       'u','b'},
    { "tx_frequency",      funcNone,       'u','b'},
    { "app_focus",      funcNone,       'u','b'},
    { "set_in_focus",      funcNone,       'u','b'},
    { "keyer",      funcNone,       'u','b'},
    { "rx_sensors_enable",      funcNone,       'u','b'},
    { "tx_sensors_enable",      funcNone,       'u','b'},
    { "rx_sensors",      funcNone,       'u','b'},
    { "tx_sensors",      funcNone,       'u','b'},
    { "audio_stream_sample_type",      funcNone,       'u','b'},
    { "audio_stream_channels",      funcNone,       'u','b'},
    { "audio_stream_samples",      funcNone,       'u','b'},
    { "digl_offset",      funcNone,       'u','b'},
    { "digu_offset",      funcNone,       'u','b'},
    { "", funcNone, 0x0 },
};

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
    pSocket->sendTextMessage(QString("vfo:0,0,%0;").arg(queue->getCache(funcSelectedFreq,false).value.value<freqt>().Hz));
    pSocket->sendTextMessage(QString("modulation:0,%0;").arg(queue->getCache(funcSelectedMode,false).value.value<modeInfo>().name.toLower()));
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
    if (cmd.toLower() == "modulation")
    {
        reply = QString("%0:%1,%2;").arg(cmd).arg(sub)
                    .arg(queue->getCache(sub?funcUnselectedFreq:funcSelectedMode,sub).value.value<modeInfo>().name);
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
                        .arg(queue->getCache(sub?funcUnselectedFreq:funcSelectedFreq,sub).value.value<freqt>().Hz);
        }
        else if (arg.size() == 3) {
            qInfo() << "Freq" << arg[2];
            freqt f;
            f.Hz = arg[2].toUInt();
            f.MHzDouble = f.Hz / (double)1E6;
            queue->add(priorityImmediate,queueItem(sub?funcUnselectedFreq:funcSelectedFreq,QVariant::fromValue(f),false,sub));
        }
    }
    else if (cmd == "modulation")
    {
        if (arg.size() == 1) {
            reply = QString("modulation:%0,%1;").arg(
                        QString::number(sub),queue->getCache(sub?funcUnselectedMode:funcSelectedMode,sub).value.value<modeInfo>().name.toLower());
        }
        else if (arg.size() == 2) {
            qInfo() << "Mode (TODO)" << arg[1];
            reply = QString("modulation:%0,%1;").arg(
                        QString::number(sub),queue->getCache(sub?funcUnselectedMode:funcSelectedMode,sub).value.value<modeInfo>().name.toLower());
            /*
            freqt f;
            f.Hz = arg[2].toUInt();
            f.MHzDouble = f.Hz / (double)1E6;
            queue->add(priorityImmediate,queueItem(sub?funcUnselectedFreq:funcSelectedFreq,QVariant::fromValue(f),false,sub));
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
            case funcSelectedFreq:
            case funcUnselectedFreq:
                reply = QString("vfo:0,%0,%1;").arg(QString::number(item.vfo),item.value.value<freqt>().Hz);
                break;
            case funcModeTR:
            case funcSelectedMode:
            case funcUnselectedMode:
                reply = QString("modulation:%0,%1;").arg(QString::number(item.vfo),item.value.value<modeInfo>().name.toLower());
                break;
            case funcTransceiverStatus:
                reply = QString("trx:%0,%1;").arg(QString::number(item.vfo),item.value.value<bool>()?"true":"false");
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
