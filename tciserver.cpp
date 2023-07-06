#include "tciserver.h"

#include "logcategories.h"



static const tciCommandStruct tci_commands[] =
    {
    { "vfo_limits",         funcNone,       'l', 'l'},
    { "if_limits",          funcNone,       'l', 'l'},
    { "trx_count",          funcNone,       'u'},
    { "channel_count",      funcNone,       'u'},
    { "device",             funcNone,       't'},
    { "receive_only",       funcNone,       'u'},
    { "modulations_list",   funcNone  },
    { "tx_enable",          funcNone,       'u','u'},
    { "ready",              funcNone, },
    { "tx_footswitch",      funcNone,       'u','b'},
    { "start",              funcNone  },
    { "stop",               funcNone  },
    { "dds",                funcNone,       'u','u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "trx_count",          funcNone,       'u'},
    { "", funcNone, 0x0 },
        };

tciServer::tciServer(quint16 port, QObject *parent) :
    QObject(parent),
    server(new QWebSocketServer(QStringLiteral("TCI Server"), QWebSocketServer::NonSecureMode, this))
{

    if (server->listen(QHostAddress::Any,port)) {
        qInfo() << "tciServer() listening on" << port;
        connect (server, &QWebSocketServer::newConnection, this, &tciServer::onNewConnection);
        connect (server, &QWebSocketServer::closed, this, &tciServer::socketDisconnected);
    }
    queue = cachingQueue::getInstance(this);

    connect(queue,SIGNAL(cacheUpdated(cacheItem)),this,SLOT(receiveCache(cacheItem)));
}

tciServer::~tciServer()
{
    server->close();
    auto it = clients.begin();

    if (it != clients.end())
    {
        it.key()->deleteLater();
        it = clients.erase(it);
    }

    //qDeleteAll(clients.begin(), clients.end());
}

void tciServer::onNewConnection()
{

    this->setObjectName("TCI Server");
    QWebSocket *pSocket = server->nextPendingConnection();

    connect(pSocket, &QWebSocket::textMessageReceived, this, &tciServer::processIncomingTextMessage);
    connect(pSocket, &QWebSocket::binaryMessageReceived, this, &tciServer::processIncomingBinaryMessage);
    connect(pSocket, &QWebSocket::disconnected, this, &tciServer::socketDisconnected);

    qInfo() << "TCI client connected:" << pSocket;
    clients.insert(pSocket,connStatus());
    pSocket->sendTextMessage(QString("protocol:WFVIEW,1.5;\n"));
    pSocket->sendTextMessage(QString("device:WFVIEW;\n"));
    pSocket->sendTextMessage(QString("receive_only:false;\n"));
    pSocket->sendTextMessage(QString("trx_count:1;\n"));
    pSocket->sendTextMessage(QString("channel_count:1;\n"));
    pSocket->sendTextMessage(QString("vfo_limits:10000,52000000;\n"));
    pSocket->sendTextMessage(QString("if_limits:-48000,48000;\n"));
    pSocket->sendTextMessage(QString("modulations_list:AM,LSB,USB,CW,NFM,WSPR,FT8,FT4,JT65,JT9,RTTY,BPSK,DIGL,DIGU,WFM,DRM;\n"));
    pSocket->sendTextMessage(QString("iq_samplerate:48000;\n"));
    pSocket->sendTextMessage(QString("audio_samplerate:48000;\n"));
    pSocket->sendTextMessage(QString("mute:false;\n"));
    pSocket->sendTextMessage(QString("vfo:0,0,%0;").arg(queue->getCache(funcSelectedFreq,false).value.value<freqt>().Hz));
    pSocket->sendTextMessage(QString("modulation:0,%0;").arg(queue->getCache(funcSelectedMode,false).value.value<modeInfo>().name));
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
        //if (arg.size() == 1)
        //it.value().rxaudio=false;
    }
    else if (cmd == "vfo")
    {
        reply = QString("%0:%1,%2,%3;").arg(cmd).arg(arg[0]).arg(arg[1])
                    .arg(queue->getCache(sub?funcUnselectedFreq:funcSelectedFreq,sub).value.value<freqt>().Hz);
    }

    if (pClient && !reply.isEmpty()) {
        qInfo() << "Reply:" << reply;
        pClient->sendTextMessage(reply);
    }
}

void tciServer::processIncomingBinaryMessage(QByteArray message)
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    qInfo() << "TCI Binary Message received:" << message;
    if (pClient) {
        //pClient->sendBinaryMessage(message);
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

void tciServer::receiveFloat(Eigen::VectorXf audio)
{
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

    rxAudioData.resize(iqHeaderSize + 2*audio.size()*sizeof (float));
    dataStream *pStream = reinterpret_cast<dataStream*>(rxAudioData.data());
    pStream->receiver = 0;
    pStream->sampleRate = 48000;
    pStream->format = IqFloat32;
    pStream->codec = 0u;
    pStream->type = RxAudioStream;
    pStream->crc = 0u;
    pStream->length = 2 * audio.size();

    Eigen::VectorXf samples(audio.size() * 2);
    Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samples.data(), audio.size()) = audio;
    Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samples.data() + 1, audio.size()) = audio;

    memcpy(pStream->data,samples.data(),samples.size()*sizeof(float));
    auto it = clients.begin();
    while (it != clients.end())
    {
        if (it.value().connected && it.value().rxaudio)
        {
            //shvqInfo() << "Sending audio to client" << it.key() << "size" << audio.size();
            it.key()->sendBinaryMessage(rxAudioData);
        }
        ++it;
    }

}


void tciServer::receiveCache(cacheItem item)
{
    if (item.command != funcSMeter)
        qInfo() << "Changed Cache" << funcString[item.command];

    auto it = clients.begin();
    while (it != clients.end())
    {
        if (it.value().connected)
        {

            switch (item.command)
            {
            case funcFreqTR:
            case funcSelectedFreq:
            case funcUnselectedFreq:
                it.key()->sendTextMessage(QString("vfo:0,%0,%1;").arg(QString::number(item.sub)).arg(item.value.value<freqt>().Hz));
                break;
            case funcModeTR:
            case funcSelectedMode:
            case funcUnselectedMode:
                it.key()->sendTextMessage(QString("modulation:%0,%1;").arg(QString::number(item.sub)).arg(item.value.value<modeInfo>().name));
                break;
            default:
                break;
            }

        }
        ++it;
    }


}
