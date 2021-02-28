#ifndef UDPSERVER_H
#define UDPSERVER_H

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

// Allow easy endian-ness conversions
#include <QtEndian>

#include <QDebug>

#include <udpserversetup.h>
#include "packettypes.h"
#include "rigidentities.h"
#include "audiohandler.h"

extern void passcode(QString in,QByteArray& out);
extern QByteArray parseNullTerminatedString(QByteArray c, int s);

struct SEQBUFENTRY {
	QTime	timeSent;
	uint16_t seqNum;
	QByteArray data;
	quint8 retransmitCount;
};

class udpServer : public QObject
{
	Q_OBJECT

public:
	udpServer(SERVERCONFIG config);
	~udpServer();

public slots:
	void init();
	void dataForServer(QByteArray);
	void receiveAudioData(const AUDIOPACKET &data);
	void receiveRigCaps(rigCapabilities caps);

signals:
	void haveDataFromServer(QByteArray);

private:

	struct CLIENT {
		bool connected = false;
		QHostAddress ipAddress;
		quint16 port;
		QByteArray clientName;
		QDateTime	timeConnected;
		QDateTime lastHeard;
		bool isStreaming;
		quint16 civPort;
		quint16 audioPort;
		quint16 txBufferLen;
		quint32 myId;
		quint32 remoteId;
		quint16 txSeq=0;
		quint16 rxSeq;
		quint16 connSeq;
		quint16 pingSeq;
		quint32 rxPingTime; // 32bit as has other info
		quint32 authInnerSeq;
		quint16 authSeq;
		quint16 innerSeq;
		quint16 sendAudioSeq;
		quint32 ident;
		quint16 tokenRx;
		quint32 tokenTx;
		quint32 commonCap;
		quint8 wdseq;
		QUdpSocket* socket;


		QTimer* pingTimer;
		QTimer* idleTimer;
		QTimer* wdTimer;
		QTimer* retransmitTimer;

		// Only used for audio.
		quint8 rxCodec;
		quint8 txCodec;
		quint16 rxSampleRate;
		quint16 txSampleRate;
		SERVERUSER user;

		QVector <SEQBUFENTRY> txSeqBuf;
		QVector <quint16> rxSeqBuf;
		QVector <SEQBUFENTRY> rxMissing;

	};

	void controlReceived();
	void civReceived();
	void audioReceived();
	void commonReceived(QList<CLIENT*>* l,CLIENT* c, QByteArray r);

	void sendPing(QList<CLIENT*> *l,CLIENT* c, quint16 seq, bool reply);
	void sendControl(CLIENT* c, quint8 type, quint16 seq);
	void sendLoginResponse(CLIENT* c, bool allowed);
	void sendCapabilities(CLIENT* c);
	void sendConnectionInfo(CLIENT* c);
	void sendTokenResponse(CLIENT* c,quint8 type);
	void sendStatus(CLIENT* c);
	void sendRetransmitRequest(CLIENT* c);
	void watchdog(CLIENT* c);
	void deleteConnection(QList<CLIENT*> *l, CLIENT* c);

	SERVERCONFIG config;

	QUdpSocket* udpControl = Q_NULLPTR;
	QUdpSocket* udpCiv = Q_NULLPTR;
	QUdpSocket* udpAudio = Q_NULLPTR;
	QHostAddress localIP;
	QString macAddress;
	
	quint32 controlId = 0;
	quint32 civId = 0;
	quint32 audioId = 0;

	QString rigname = "IC-9700";
	quint8 rigciv = 0xa2;

	QMutex mutex; // Used for critical operations.

	QList <CLIENT*> controlClients = QList<CLIENT*>();
	QList <CLIENT*> civClients = QList<CLIENT*>();
	QList <CLIENT*> audioClients = QList<CLIENT*>();
	QTime timeStarted;
	rigCapabilities rigCaps;
};


#endif // UDPSERVER_H