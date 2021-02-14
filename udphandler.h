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

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QtMultimedia/QAudioOutput>
#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "audiohandler.h"

#define PURGE_SECONDS 5
#define TOKEN_RENEWAL 60000
#define PING_PERIOD 100
#define IDLE_PERIOD 100
#define TXAUDIO_PERIOD 10
#define AREYOUTHERE_PERIOD 500


quint8* passcode(QString str);
QByteArray parseNullTerminatedString(QByteArray c, int s);

// Parent class that contains all common items.
class udpBase : public QObject
{

public:
	~udpBase();

	void init();

	void dataReceived(QByteArray r); 
	void sendAreYouThere();
	void sendPing(); // Periodic type 0x07 ping packet sending
	void sendIdle(bool tracked, quint16 seq);

	QUdpSocket* udp=Q_NULLPTR;
	uint32_t myId = 0;
	uint32_t remoteId = 0;
	char authID[6] = { 0, 0, 0, 0, 0, 0 };
	char a8replyID[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint16_t authInnerSendSeq = 0;
	uint16_t innerSendSeq = 0x8304; // Not sure why?
	uint16_t sendSeqB = 0;
	uint16_t sendSeq = 1;
	uint16_t lastReceivedSeq = 0;
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
	time_t	lastReceived = time(0);
	QMutex mutex;

	struct SEQBUFENTRY {
		time_t	timeSent;
		uint16_t seqNum;
		QByteArray data;
	};

	QList <SEQBUFENTRY> txSeqBuf = QList<SEQBUFENTRY>();
	QList <SEQBUFENTRY> seqBuf = QList<SEQBUFENTRY>();

	void sendAreYouReady();
	void sendPacketDisconnect();
	void sendTrackedPacket(QByteArray d);
	void purgeOldEntries();

	QTimer areYouThereTimer; // Send are-you-there packets every second until a response is received.
	QTimer pingTimer; // Start sending pings immediately.
	QTimer idleTimer; // Start watchdog once we are connected.

	QDateTime lastPingSentTime;
	uint16_t pingSendSeq = 0;

	QDateTime lastControlPacketSentTime;

	quint16 areYouThereCounter=0;

	quint32 packetsSent=0;
	quint32 packetsLost=0;

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
	void dataReceived();
	void SendIdle();
	void SendPeriodic();
	void sendOpenClose(bool close);
};


// Class for all audio communications.
class udpAudio : public udpBase
{
	Q_OBJECT

public:
	udpAudio(QHostAddress local, QHostAddress ip, quint16 aport, quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec);
	~udpAudio();

signals:
    void haveAudioData(QByteArray data);

	void setupTxAudio(const quint8 samples, const quint8 channels, const quint16 samplerate, const quint16 bufferSize, const bool isUlaw, const bool isInput);
	void setupRxAudio(const quint8 samples, const quint8 channels, const quint16 samplerate, const quint16 bufferSize, const bool isUlaw, const bool isInput);

	void haveChangeBufferSize(quint16 value);

public slots:
	void changeBufferSize(quint16 value);

private:

	void sendTxAudio();
	void dataReceived();
	QAudioFormat format;
	quint16 bufferSize;
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

	QTimer txAudioTimer;

};



// Class to handle the connection/disconnection of the radio.
class udpHandler: public udpBase
{
	Q_OBJECT

public:
	udpHandler(QString ip, quint16 cport, quint16 sport, quint16 aport, QString username, QString password, 
					quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec);
	~udpHandler();

	bool serialAndAudioOpened = false;

	udpCivData* civ = Q_NULLPTR;
	udpAudio* audio = Q_NULLPTR;


public slots:
	void receiveDataFromUserToRig(QByteArray); // This slot will send data on to 
	void receiveFromCivStream(QByteArray);
	void changeBufferSize(quint16 value);


signals:
	void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
	void haveNetworkError(QString, QString);
	void haveNetworkStatus(QString);
	void haveChangeBufferSize(quint16 value);

private:

	void sendAreYouThere();

	void dataReceived();

	void sendRequestSerialAndAudio();
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
	quint16 rxBufferSize;
	quint8 rxCodec;
	quint8 txCodec;

	quint16 reauthInterval = 60000;
	QByteArray devName;
	QByteArray compName;
	
	QTimer tokenTimer;
	QTimer areYouThereTimer;

	bool highBandwidthConnection = false;
};


#endif
