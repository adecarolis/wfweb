#ifndef KENWOODSERVER_H
#define KENWOODSERVER_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSet>
#include <QDataStream>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QByteArray>
#include <QList>
#include <QVector>
#include <QMap>

// Allow easy endian-ness conversions
#include <QtEndian>

#include <QDebug>

#include "rigserver.h"
#include "packettypes.h"
#include "rigidentities.h"
#include "audiohandler.h"
#include "rigcommander.h"
#include "kenwoodcommander.h"

class kenwoodServer : public rigServer
{
	Q_OBJECT

public:

    kenwoodServer(SERVERCONFIG* config, rigServer* parent = nullptr);
    ~kenwoodServer();

public slots:
	void init();
	void dataForServer(QByteArray);
	void receiveAudioData(const audioPacket &data);

private slots:
    void disconnected(QTcpSocket* socket);
    void readyRead(QTcpSocket* socket);

private:

    void newConnection();

	struct CLIENT {
        bool connected = false;
        bool authenticated = false;
        bool admin = false;
        bool tx = false;
        QString type;
        QString user;
		QHostAddress ipAddress;
		quint16 port;
        QByteArray clientName;
        QByteArray buffer;
		QDateTime	timeConnected;
		QDateTime lastHeard;
		bool isStreaming;
		quint16 civPort;
		quint16 audioPort;
		quint16 txBufferLen;
        quint8 guid[GUIDLEN];

	};


    QTcpServer* server = Q_NULLPTR;
    QMap <QTcpSocket*, CLIENT*> clients;

};


#endif // KENWOODSERVER_H
