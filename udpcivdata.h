// Class for all (pseudo) serial communications
#ifndef UDPCIVDATA_H
#define UDPCIVDATA_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QUuid>

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "packettypes.h"

#include "udpbase.h"

class udpCivData : public udpBase
{
	Q_OBJECT

public:
	udpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, bool splitWf, quint16 lport);
	~udpCivData();
	QMutex serialmutex;

signals:
	int receive(QByteArray);

public slots:
	void send(QByteArray d);


private:
	void watchdog();
	void dataReceived();
	void sendOpenClose(bool close);

	QTimer* startCivDataTimer = Q_NULLPTR;
	bool splitWaterfall = false;
};


#endif