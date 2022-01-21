#ifndef UDPHANDLER_H
#define UDPHANDLER_H

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

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "audiohandler.h"
#include "packettypes.h"


struct udpPreferences {
	QString ipAddress;
	quint16 controlLANPort;
	quint16 serialLANPort;
	quint16 audioLANPort;
	QString username;
	QString password;
	QString clientName;
};

void passcode(QString in, QByteArray& out);
QByteArray parseNullTerminatedString(QByteArray c, int s);

// Parent class that contains all common items.
class udpBase : public QObject
{


public:
	~udpBase();

	void init();

	void dataReceived(QByteArray r); 
	void sendPing();
	void sendRetransmitRange(quint16 first, quint16 second, quint16 third,quint16 fourth);

	void sendControl(bool tracked,quint8 id, quint16 seq);

	QTime timeStarted;

	QUdpSocket* udp=Q_NULLPTR;
	uint32_t myId = 0;
	uint32_t remoteId = 0;
	uint8_t authSeq = 0x00;
	uint16_t sendSeqB = 0;
	uint16_t sendSeq = 1;
	uint16_t lastReceivedSeq = 1;
	uint16_t pkt0SendSeq = 0;
	uint16_t periodicSeq = 0;
	quint64 latency = 0;

	QString username = "";
	QString password = "";
	QHostAddress radioIP;
	QHostAddress localIP;
	bool isAuthenticated = false;
	quint16 localPort=0;
	quint16 port=0;
	bool periodicRunning = false;
	bool sentPacketConnect2 = false;
	QTime	lastReceived =QTime::currentTime();
	QMutex udpMutex;
	QMutex txBufferMutex;
	QMutex rxBufferMutex;
	QMutex missingMutex;

	struct SEQBUFENTRY {
		QTime	timeSent;
		uint16_t seqNum;
		QByteArray data;
		quint8 retransmitCount;
	};

	QMap<quint16, QTime> rxSeqBuf;
	QMap<quint16, SEQBUFENTRY> txSeqBuf;
	QMap<quint16, int> rxMissing;

	void sendTrackedPacket(QByteArray d);
	void purgeOldEntries();

	QTimer* areYouThereTimer = Q_NULLPTR; // Send are-you-there packets every second until a response is received.
	QTimer* pingTimer = Q_NULLPTR; // Start sending pings immediately.
	QTimer* idleTimer = Q_NULLPTR; // Start watchdog once we are connected.

	QTimer* watchdogTimer = Q_NULLPTR;
	QTimer* retransmitTimer = Q_NULLPTR;

	QDateTime lastPingSentTime;
	uint16_t pingSendSeq = 0;

	quint16 areYouThereCounter=0;

	quint32 packetsSent=0;
	quint32 packetsLost=0;

	quint16 seqPrefix = 0;
	QString connectionType="";
	int congestion = 0;


private:
	void sendRetransmitRequest();

};


// Class for all (pseudo) serial communications
class udpCivData : public udpBase
{
	Q_OBJECT

public:
	udpCivData(QHostAddress local, QHostAddress ip, quint16 civPort);
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
};


// Class for all audio communications.
class udpAudio : public udpBase
{
	Q_OBJECT

public:
	udpAudio(QHostAddress local, QHostAddress ip, quint16 aport, audioSetup rxSetup, audioSetup txSetup);
	~udpAudio();

	int audioLatency = 0;

signals:
	void haveAudioData(audioPacket data);

	void setupTxAudio(audioSetup setup);
	void setupRxAudio(audioSetup setup);

	void haveChangeLatency(quint16 value);
	void haveSetVolume(unsigned char value);

public slots:
	void changeLatency(quint16 value);
	void setVolume(unsigned char value);

private:

	void sendTxAudio();
	void dataReceived();
	void watchdog();

	uint16_t sendAudioSeq = 0;

	audioHandler* rxaudio = Q_NULLPTR;
	QThread* rxAudioThread = Q_NULLPTR;

	audioHandler* txaudio = Q_NULLPTR;
	QThread* txAudioThread = Q_NULLPTR;

	QTimer* txAudioTimer=Q_NULLPTR;
	bool enableTx = true;

	QMutex audioMutex;

};



// Class to handle the connection/disconnection of the radio.
class udpHandler: public udpBase
{
	Q_OBJECT

public:
	udpHandler(udpPreferences prefs, audioSetup rxAudio, audioSetup txAudio);
	~udpHandler();

	bool streamOpened = false;

	udpCivData* civ = Q_NULLPTR;
	udpAudio* audio = Q_NULLPTR;

	unsigned char numRadios;
	QList<radio_cap_packet> radios;

public slots:
	void receiveDataFromUserToRig(QByteArray); // This slot will send data on to 
	void receiveFromCivStream(QByteArray);
	void receiveAudioData(const audioPacket &data);
	void changeLatency(quint16 value);
	void setVolume(unsigned char value);
	void init();

signals:
	void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
	void haveAudioData(audioPacket data); // emit this when we have data, connect to rigcommander
	void haveNetworkError(QString, QString);
	void haveChangeLatency(quint16 value);
	void haveSetVolume(unsigned char value);
	void haveNetworkStatus(QString);
	void haveBaudRate(quint32 baudrate);
	void requestRadioSelection(QList<radio_cap_packet> radios);
	void setRadioUsage(int, QString name, QString mac);
private:
	
	void sendAreYouThere();

	void dataReceived();

	void sendRequestStream();
	void sendLogin();
	void sendToken(uint8_t magic);

	bool gotA8ReplyID = false;
	bool gotAuthOK = false;

	bool sentPacketLogin = false;
	bool sentPacketConnect = false;
	bool sentPacketConnect2 = false;

	bool radioInUse = false;

	quint16 controlPort;
	quint16 civPort;
	quint16 audioPort;

	audioSetup rxSetup;
	audioSetup txSetup;

	quint16 reauthInterval = 60000;
	QString devName;
	QString compName;
	QString audioType;
	//QByteArray replyId;
	quint16 tokRequest;
	quint32 token;
	// These are for stream ident info.
	quint8 macaddress[8];

	QByteArray usernameEncoded;
	QByteArray passwordEncoded;

	QTimer* tokenTimer = Q_NULLPTR;
	QTimer* areYouThereTimer = Q_NULLPTR;

	bool highBandwidthConnection = false;
	quint8 civId = 0;
	quint16 rxSampleRates = 0;
	quint16 txSampleRates = 0;
};


#endif
