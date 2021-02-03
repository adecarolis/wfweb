#ifndef UDPHANDLER_H
#define UDPHANDLER_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QtMultimedia/qaudiooutput>
#include <QBuffer>


#include <QDebug>

// Parent class that contains all common items.
class udpBase : public QObject
{

public:
	qint64 SendTrackedPacket(QByteArray d);
	qint64 SendPacketConnect();
	qint64 SendPacketConnect2();
	qint64 SendPacketDisconnect();
	void SendPkt0Idle();
	void SendPkt7Idle();
	void PurgeOldEntries();

	unsigned char* Passcode(QString str);
	QString parseNullTerminatedString(QByteArray c, int s);
	QUdpSocket* udp;
	uint32_t localSID = 0;
	uint32_t remoteSID = 0;
	char authID[6] = { 0, 0, 0, 0, 0, 0 };
	char a8replyID[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint16_t authInnerSendSeq = 0;
	uint16_t innerSendSeq = 0;
	uint16_t sendSeqB = 0;
	uint16_t sendSeq = 1;
	uint16_t pkt0SendSeq = 0;
	uint16_t pkt7SendSeq = 0;
	uint16_t periodicSeq = 0;
	time_t lastPacket0Sent;
	time_t lastPacket7Sent;

	QString username = "";
	QString password = "";
	QHostAddress radioIP;
	QHostAddress localIP;
	bool isAuthenticated = false;
	int localPort;
	int port;
	QTimer *pkt7Timer=nullptr; // Send pkt7 packets every 3 seconds
	QTimer *pkt0Timer=nullptr; // Send pkt0 packets every 1000ms.
	QTimer *periodic=nullptr; // Send pkt0 packets every 1000ms.
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
	udpSerial(QHostAddress local, QHostAddress ip, int sport);
	~udpSerial();
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
	udpAudio(QHostAddress local, QHostAddress ip, int aport);
	~udpAudio();

private:

	void DataReceived();

	QBuffer* buffer;
	QAudioOutput* audio;
	QAudioFormat format;

	bool sentPacketConnect2 = false;
	uint16_t lastSeq = 0;
	uint16_t sendAudioSeq = 0;

};



// Class to handle the connection/disconnection of the radio.
class udpHandler: public udpBase
{
	Q_OBJECT

public:
	udpHandler(QHostAddress ip, int cport, int sport, int aport, QString username, QString password);
	~udpHandler();

	udpSerial *serial=nullptr;
	udpAudio *audio=nullptr;

	bool serialAndAudioOpened = false;



public slots:
	void receiveDataFromUserToRig(QByteArray); // This slot will send data on to 
	void receiveFromSerialStream(QByteArray);


signals:
	void RigConnected(const QString&);
	void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander

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

	int aport;
	int sport;
	int reauthInterval = 60000;
	QTimer reauthTimer;
	QString devname;
};


#endif