#ifndef TCISERVER_H
#define TCISERVER_H

#include <QObject>
#include <QtWebSockets/QtWebSockets>
#include <QtWebSockets/QWebSocketServer>

#include "cachingqueue.h"

/* Opus and Eigen */
#ifdef Q_OS_WIN
#include <Eigen/Eigen>
#else
#include <eigen3/Eigen/Eigen>
#endif

struct tciCommandStruct
{
    const char *str;
    funcs func;
    char arg1=0;
    char arg2=0;
    char arg3=0;
};


class tciServer : public QObject
{
    Q_OBJECT

    static constexpr quint32 iqHeaderSize = 16u*sizeof(quint32);
    static constexpr quint32 iqBufferSize = 2048u;

    typedef enum
    {
        IqInt16 = 0,
        IqInt24,
        IqInt32,
        IqFloat32,
        IqFloat64
    }iqDataType;

    typedef enum
    {
        IqStream = 0,
        RxAudioStream,
        TxAudioStream,
        TxChrono,
    }streamType;

    typedef struct
    {
        quint32 receiver;
        quint32 sampleRate;
        quint32 format;
        quint32 codec;
        quint32 crc;
        quint32 length;
        quint32 type;
        quint32 reserv[9];
        float   data[4096];
    }dataStream;

    struct connStatus {
        bool connected=true;
        bool rxaudio=false;
        bool txaudio=false;
    };


public:
    explicit tciServer(quint16 port, QObject *parent = nullptr);
    ~tciServer();

signals:
    void closed();

public slots:
    void receiveFloat(Eigen::VectorXf audio);

private slots:
    void onNewConnection();
    void processIncomingTextMessage(QString message);
    void processIncomingBinaryMessage(QByteArray message);
    void socketDisconnected();
    void receiveCache(cacheItem item);

private:
    QWebSocketServer *server;
    QMap<QWebSocket *, connStatus> clients;
    cachingQueue *queue;
    QByteArray rxAudioData;
    QByteArray txAudioData;
};

#endif // TCISERVER_H
