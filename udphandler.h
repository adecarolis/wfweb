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

// Parent class that contains all common items.
class udpBase : public QObject
{

public:
	~udpBase();

	void init();

	qint64 SendTrackedPacket(QByteArray d);
	qint64 SendPacketConnect();
	qint64 SendPacketConnect2();
	qint64 SendPacketDisconnect();
	void SendPkt0Idle(bool tracked, quint16 seq);
	void SendPkt7Idle();
	void PurgeOldEntries();

	void DataReceived(QByteArray r);

	unsigned char* Passcode(QString str);
	QByteArray parseNullTerminatedString(QByteArray c, int s);
	QUdpSocket* udp=Q_NULLPTR;
	uint32_t localSID = 0;
	uint32_t remoteSID = 0;
	char authID[6] = { 0, 0, 0, 0, 0, 0 };
	char a8replyID[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint16_t authInnerSendSeq = 0;
	uint16_t innerSendSeq = 0x8304; // Not sure why?
	uint16_t sendSeqB = 0;
	uint16_t sendSeq = 1;
	uint16_t lastReceivedSeq = 0;
	uint16_t pkt0SendSeq = 0;
	uint16_t pkt7SendSeq = 0;
	uint16_t periodicSeq = 0;
	QDateTime lastPacket0Sent;
	QDateTime lastPacket7Sent;
	quint64 latency = 0;

	QString username = "";
	QString password = "";
	QHostAddress radioIP;
	QHostAddress localIP;
	bool isAuthenticated = false;
	quint16 localPort=0;
	quint16 port=0;
	QTimer *pkt7Timer=Q_NULLPTR; // Send pkt7 packets every 3 seconds
	QTimer *pkt0Timer=Q_NULLPTR; // Send pkt0 packets every 1000ms.
	QTimer *periodic=Q_NULLPTR; // Send pkt0 packets every 1000ms.
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

};


// Class for all (pseudo) serial communications
class udpSerial : public udpBase
{
	Q_OBJECT

public:
	udpSerial(QHostAddress local, QHostAddress ip, quint16 sport);
	QMutex serialmutex;

signals:
	//void ReceiveSerial(QByteArray);
	int Receive(QByteArray);

public slots:
	int Send(QByteArray d);


private:
	void DataReceived();
	void SendIdle();
	void SendPeriodic();
	qint64 SendPacketOpenClose(bool close);
};


// Class for all audio communications.
class udpAudio : public udpBase
{
	Q_OBJECT

public:
	udpAudio(QHostAddress local, QHostAddress ip, quint16 aport, quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec);
	~udpAudio();
	QAudioOutput* audio;

signals:
    void haveAudioData(QByteArray data);
	void setupTxAudio(const quint8 samples, const quint8 channels, const quint16 samplerate, const quint16 bufferSize, const bool isUlaw, const bool isInput);
	void setupRxAudio(const quint8 samples, const quint8 channels, const quint16 samplerate, const quint16 bufferSize, const bool isUlaw, const bool isInput);
	void haveChangeBufferSize(quint16 value);

public slots:
	void changeBufferSize(quint16 value);
	void sendTxAudio(QByteArray d);


private:

	void DataReceived();
	QAudioFormat format;
	quint16 bufferSize;
	quint16 rxSampleRate;
	quint16 txSampleRate;
	quint8 rxCodec;
	quint8 txCodec;
	quint8 rxChannelCount = 1;
	bool rxIsUlawCodec = false;
	quint8 rxNumSamples = 16;
	quint8 txChannelCount = 1;
	bool txIsUlawCodec = false;
	quint8 txNumSamples = 16;

	bool sentPacketConnect2 = false;
	uint16_t sendAudioSeq = 0;

	audioHandler* rxaudio;
	QThread* rxAudioThread;

	audioHandler* txaudio;
	QThread* txAudioThread;
};



// Class to handle the connection/disconnection of the radio.
class udpHandler: public udpBase
{
	Q_OBJECT

public:
	udpHandler(QString ip, quint16 cport, quint16 sport, quint16 aport, QString username, QString password, 
					quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec);
	~udpHandler();

	udpSerial *serial=Q_NULLPTR;
	udpAudio *audio=Q_NULLPTR;

	bool serialAndAudioOpened = false;



public slots:
	void receiveDataFromUserToRig(QByteArray); // This slot will send data on to 
	void receiveFromSerialStream(QByteArray);
	void changeBufferSize(quint16 value);


signals:
	void RigConnected(const QString&);
	void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
	void haveNetworkError(QString, QString);
	void haveNetworkStatus(QString);
	void haveChangeBufferSize(quint16 value);

private:

	qint64 SendRequestSerialAndAudio();
	qint64 SendPacketLogin();
	qint64 SendPacketAuth(uint8_t magic);
	void ReAuth();
	void DataReceived();
	bool gotA8ReplyID = false;
	bool gotAuthOK = false;

	bool sentPacketLogin = false;
	bool sentPacketConnect = false;
	bool sentPacketConnect2 = false;

	bool radioInUse = false;

	quint16 aport;
	quint16 sport;
	quint16 rxSampleRate;
	quint16 txSampleRate;
	quint16 rxBufferSize;
	quint8 rxCodec;
	quint8 txCodec;

	quint16 reauthInterval = 60000;
	QTimer reauthTimer;
	QByteArray devName;
	QByteArray compName;
};


#endif
