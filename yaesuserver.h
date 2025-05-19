#ifndef YAESUSERVER_H
#define YAESUSERVER_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QNetworkInterface>
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

class yaesuServer : public rigServer
{
	Q_OBJECT

public:

    yaesuServer(SERVERCONFIG* config, rigServer* parent = nullptr);
    ~yaesuServer();

public slots:
	void init();
	void dataForServer(QByteArray);
	void receiveAudioData(const audioPacket &data);

private:

	struct CLIENT {
		bool connected = false;
		QString type;
		QHostAddress ipAddress;
		quint16 port;
        QByteArray clientName;
		QDateTime	timeConnected;
		QDateTime lastHeard;
		bool isStreaming;
		quint16 civPort;
		quint16 audioPort;
		quint16 txBufferLen;
	};
};


#endif // YAESUSERVER_H
