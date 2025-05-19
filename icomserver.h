#ifndef ICOMSERVER_H
#define ICOMSERVER_H

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
#include "udphandler.h"
#include "audiohandler.h"
#include "rigcommander.h"

extern void passcode(QString in,QByteArray& out);
extern QByteArray parseNullTerminatedString(QByteArray c, int s);

struct SEQBUFENTRY {
	QTime	timeSent;
	uint16_t seqNum;
	QByteArray data;
	quint8 retransmitCount;
};


class icomServer : public rigServer
{
	Q_OBJECT

public:

    icomServer(SERVERCONFIG* config, rigServer* parent = nullptr);
    ~icomServer();

public slots:
	void init();
	void dataForServer(QByteArray);
	void receiveAudioData(const audioPacket &data);
    void receiveRigCaps(rigCapabilities* caps);

signals:
	void haveDataFromServer(QByteArray);
	void haveAudioData(audioPacket data);
	void haveNetworkStatus(networkStatus);

	void setupTxAudio(audioSetup);
	void setupRxAudio(audioSetup);



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
		quint32 myId;
		quint32 remoteId;
		quint16 txSeq=0;
		quint16 rxSeq;
		quint16 connSeq;
		quint16 pingSeq;
		quint32 rxPingTime; // 32bit as has other info
		quint16 authInnerSeq;
		quint16 authSeq;
		quint16 innerSeq;
		quint16 sendAudioSeq;
		quint8 macaddress[6];
		quint16 tokenRx;
		quint32 tokenTx;
		quint32 commonCap;
		quint8 wdseq;
		QUdpSocket* socket;


		QTimer* pingTimer;
		QTimer* idleTimer;
		QTimer* retransmitTimer;

		// Only used for audio.
		quint8 rxCodec;
		quint8 txCodec;
		quint16 rxSampleRate;
		quint16 txSampleRate;
		SERVERUSER user;


		QMap<quint16, QTime> rxSeqBuf;
		QMap<quint16, SEQBUFENTRY> txSeqBuf;
		QMap<quint16, int> rxMissing;

		QMutex txMutex;
		QMutex rxMutex;
		QMutex missMutex;

		quint16 seqPrefix;

		quint8 civId;
		bool isAuthenticated;
		CLIENT* controlClient = Q_NULLPTR;
		CLIENT* civClient = Q_NULLPTR;
		CLIENT* audioClient = Q_NULLPTR;
		quint8 guid[GUIDLEN];

        qint64 startTime;
        QTime clientTime;
        qint64 timeOffset;
        qint64 timeDifference = 0;
	};

	void controlReceived();
	void civReceived();
	void audioReceived();
	void commonReceived(QList<CLIENT*>* l,CLIENT* c, QByteArray r);

	void sendPing(QList<CLIENT*> *l,CLIENT* c, quint16 seq, bool reply);
	void sendControl(CLIENT* c, quint8 type, quint16 seq);
	void sendLoginResponse(CLIENT* c, bool allowed);
	void sendCapabilities(CLIENT* c);
	void sendConnectionInfo(CLIENT* c,quint8 guid[GUIDLEN]);
	void sendTokenResponse(CLIENT* c,quint8 type);
	void sendStatus(CLIENT* c);
	void sendRetransmitRequest(CLIENT* c);
	void watchdog();
	void deleteConnection(QList<CLIENT*> *l, CLIENT* c);

	SERVERCONFIG *config;

	QUdpSocket* udpControl = Q_NULLPTR;
	QUdpSocket* udpCiv = Q_NULLPTR;
	QUdpSocket* udpAudio = Q_NULLPTR;
	QHostAddress localIP;
	quint8 macAddress[6];
	
	quint32 controlId = 0;
	quint32 civId = 0;
	quint32 audioId = 0;

	QMutex udpMutex; // Used for critical operations.
	QMutex connMutex;
	QMutex audioMutex;

	QList <CLIENT*> controlClients = QList<CLIENT*>();
	QList <CLIENT*> civClients = QList<CLIENT*>();
	QList <CLIENT*> audioClients = QList<CLIENT*>();

    //QTime timeStarted;


	audioSetup outAudio;
	audioSetup inAudio;

	quint16 rxSampleRate = 0;
	quint16 txSampleRate = 0;
	quint8 rxCodec = 0;
	quint8 txCodec = 0;

	QHostAddress hasTxAudio;
	QTimer* wdTimer;

	networkStatus status;
};


#endif // ICOMSERVER_H
