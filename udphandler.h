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

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QtMultimedia/QAudioOutput>
#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "audiohandler.h"
#include "packettypes.h"

#define PURGE_SECONDS 10
#define TOKEN_RENEWAL 60000
#define PING_PERIOD 100
#define IDLE_PERIOD 100
#define TXAUDIO_PERIOD 10 
#define AREYOUTHERE_PERIOD 500
#define WATCHDOG_PERIOD 500
#define RETRANSMIT_PERIOD 100

struct udpPreferences {
	QString ipAddress;
	quint16 controlLANPort;
	quint16 serialLANPort;
	quint16 audioLANPort;
	QString username;
	QString password;
	QString audioOutput;
	QString audioInput;
	quint16 audioRXLatency;
	quint16 audioTXLatency;
	quint16 audioRXSampleRate;
	quint8 audioRXCodec;
	quint16 audioTXSampleRate;
	quint8 audioTXCodec;
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
	uint16_t authInnerSendSeq = 0;
	uint16_t innerSendSeq = 0x8304; // Not sure why?
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

	struct SEQBUFENTRY {
		QTime	timeSent;
		uint16_t seqNum;
		QByteArray data;
		quint8 retransmitCount;
	};

	QVector<SEQBUFENTRY> txSeqBuf;

	QVector<quint16> rxSeqBuf;

	QVector<SEQBUFENTRY> rxMissing;

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
	udpAudio(QHostAddress local, QHostAddress ip, quint16 aport, quint16 rxlatency, quint16 txlatency, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec, QString outputPort, QString inputPort);
	~udpAudio();

signals:
    void haveAudioData(audioPacket data);

	void setupTxAudio(const quint8 samples, const quint8 channels, const quint16 samplerate, const quint16 latency, const bool isUlaw, const bool isInput, QString port);
	void setupRxAudio(const quint8 samples, const quint8 channels, const quint16 samplerate, const quint16 latency, const bool isUlaw, const bool isInput, QString port);

	void haveChangeLatency(quint16 value);

public slots:
	void changeLatency(quint16 value);

private:

	void sendTxAudio();
	void dataReceived();
	void watchdog();

	QAudioFormat format;
	quint16 rxLatency;
	quint16 txLatency;
	quint16 rxSampleRate;
	quint16 txSampleRate;
	quint8 rxCodec;
	quint8 txCodec;
	quint8 rxChannelCount = 1;
	bool rxIsUlawCodec = false;
	quint8 rxNumSamples = 8;
	quint8 txChannelCount = 1;
	bool txIsUlawCodec = false;
	quint8 txNumSamples = 8;

	bool sentPacketConnect2 = false;
	uint16_t sendAudioSeq = 0;

	audioHandler* rxaudio=Q_NULLPTR;
	QThread* rxAudioThread=Q_NULLPTR;

	audioHandler* txaudio=Q_NULLPTR;
	QThread* txAudioThread=Q_NULLPTR;

	QTimer* txAudioTimer=Q_NULLPTR;

};



// Class to handle the connection/disconnection of the radio.
class udpHandler: public udpBase
{
	Q_OBJECT

public:
	udpHandler(udpPreferences prefs);
	~udpHandler();

	bool streamOpened = false;

	udpCivData* civ = Q_NULLPTR;
	udpAudio* audio = Q_NULLPTR;


public slots:
	void receiveDataFromUserToRig(QByteArray); // This slot will send data on to 
	void receiveFromCivStream(QByteArray);
	void receiveAudioData(const audioPacket &data);
	void changeLatency(quint16 value);
	void init();

signals:
	void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
	void haveAudioData(audioPacket data); // emit this when we have data, connect to rigcommander
	void haveNetworkError(QString, QString);
	void haveNetworkStatus(QString);
	void haveChangeLatency(quint16 value);

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

	quint16 rxSampleRate;
	quint16 txSampleRate;
	quint16 rxLatency;
	quint16 txLatency;
	quint8 rxCodec;
	quint8 txCodec;

	QString audioInputPort;
	QString audioOutputPort;

	quint16 reauthInterval = 60000;
	QString devName;
	QString compName;
	QString audioType;
	//QByteArray replyId;
	quint16 tokRequest;
	quint32 token;
	// These are for stream ident info.
	char identa;
	quint32 identb;

	QByteArray usernameEncoded;
	QByteArray passwordEncoded;

	QTimer* tokenTimer = Q_NULLPTR;
	QTimer* areYouThereTimer = Q_NULLPTR;

	bool highBandwidthConnection = false;

};

Q_DECLARE_METATYPE(struct audioPacket)

#endif
