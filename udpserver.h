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
#include <QMap>

// Allow easy endian-ness conversions
#include <QtEndian>

#include <QDebug>

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


struct SERVERUSER {
	QString username;
	QString password;
	quint8 userType;
};

struct SERVERCONFIG {
	bool enabled;
	bool lan;
	quint16 controlPort;
	quint16 civPort;
	quint16 audioPort;
	int audioOutput;
	int audioInput;
	quint8 resampleQuality;
	quint32 baudRate;

	QList <SERVERUSER> users;
};


class udpServer : public QObject
{
	Q_OBJECT

public:
	udpServer(SERVERCONFIG config, audioSetup outAudio, audioSetup inAudio);
	~udpServer();

public slots:
	void init();
	void dataForServer(QByteArray);
	void receiveAudioData(const audioPacket &data);
	void receiveRigCaps(rigCapabilities caps);

signals:
	void haveDataFromServer(QByteArray);
	void haveAudioData(audioPacket data);
	void haveNetworkStatus(QString);

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
		quint8 authInnerSeq;
		quint16 authSeq;
		quint16 innerSeq;
		quint16 sendAudioSeq;
		quint8 identa;
		quint32 identb;
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
	void watchdog();
	void sendRxAudio();
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

	quint8 rigciv = 0xa2;

	QMutex udpMutex; // Used for critical operations.
	QMutex connMutex;
	QMutex audioMutex;

	QList <CLIENT*> controlClients = QList<CLIENT*>();
	QList <CLIENT*> civClients = QList<CLIENT*>();
	QList <CLIENT*> audioClients = QList<CLIENT*>();
	QTime timeStarted;
	rigCapabilities rigCaps;

	audioHandler* rxaudio = Q_NULLPTR;
	QThread* rxAudioThread = Q_NULLPTR;

	audioHandler* txaudio = Q_NULLPTR;
	QThread* txAudioThread = Q_NULLPTR;

	audioSetup outAudio;
	audioSetup inAudio;

	QTimer* rxAudioTimer=Q_NULLPTR;
	quint16 rxSampleRate = 0;
	quint16 txSampleRate = 0;
	quint8 rxCodec = 0;
	quint8 txCodec = 0;

	QHostAddress hasTxAudio;
	QTimer* wdTimer;
};


#endif // UDPSERVER_H