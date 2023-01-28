#ifndef UDPBASE_H
#define UDPBASE_H

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



struct udpPreferences {
	QString ipAddress;
	quint16 controlLANPort;
	quint16 serialLANPort;
	quint16 audioLANPort;
	QString username;
	QString password;
	QString clientName;
	quint8 waterfallFormat;
	bool halfDuplex;
};

struct networkAudioLevels {
    bool haveTxLevels = false;
    bool haveRxLevels = false;
    quint8 rxAudioRMS = 0;
    quint8 txAudioRMS = 0;
    quint8 rxAudioPeak = 0;
    quint8 txAudioPeak = 0;
};

struct networkStatus {
	quint8 rxAudioBufferPercent;
	quint8 txAudioBufferPercent;
	quint8 rxAudioLevel;
	quint8 txAudioLevel;
	quint16 rxLatency;
	quint16 txLatency;
	bool rxUnderrun;
	bool txUnderrun;
	bool rxOverrun;
	bool txOverrun;
	quint16 rxCurrentLatency;
	quint16 txCurrentLatency;
	quint32 packetsSent = 0;
	quint32 packetsLost = 0;
	quint16 rtt = 0;
	quint32 networkLatency = 0;
	QString message;
};


// Parent class that contains all common items.
class udpBase : public QObject
{


public:
	~udpBase();

	void init(quint16 local);

	void reconnect();

	void dataReceived(QByteArray r);
	void sendPing();
	void sendRetransmitRange(quint16 first, quint16 second, quint16 third, quint16 fourth);

	void sendControl(bool tracked, quint8 id, quint16 seq);

	void printHex(const QByteArray& pdata);
	void printHex(const QByteArray& pdata, bool printVert, bool printHoriz);


	//QTime timeStarted;

	QUdpSocket* udp = Q_NULLPTR;
	uint32_t myId = 0;
	uint32_t remoteId = 0;
	uint16_t authSeq = 0x30;
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
	quint16 localPort = 0;
	quint16 port = 0;
	bool periodicRunning = false;
	bool sentPacketConnect2 = false;
	QTime	lastReceived = QTime::currentTime();
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

	quint16 areYouThereCounter = 0;

	quint32 packetsSent = 0;
	quint32 packetsLost = 0;

	quint16 seqPrefix = 0;
	QString connectionType = "";
	int congestion = 0;


private:
	void sendRetransmitRequest();

};


/// <summary>
/// passcode function used to generate secure (ish) code
/// </summary>
/// <param name="str"></param>
/// <returns>pointer to encoded username or password</returns>
static inline void passcode(QString in, QByteArray& out)
{
	const quint8 sequence[] =
	{
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0x47,0x5d,0x4c,0x42,0x66,0x20,0x23,0x46,0x4e,0x57,0x45,0x3d,0x67,0x76,0x60,0x41,0x62,0x39,0x59,0x2d,0x68,0x7e,
		0x7c,0x65,0x7d,0x49,0x29,0x72,0x73,0x78,0x21,0x6e,0x5a,0x5e,0x4a,0x3e,0x71,0x2c,0x2a,0x54,0x3c,0x3a,0x63,0x4f,
		0x43,0x75,0x27,0x79,0x5b,0x35,0x70,0x48,0x6b,0x56,0x6f,0x34,0x32,0x6c,0x30,0x61,0x6d,0x7b,0x2f,0x4b,0x64,0x38,
		0x2b,0x2e,0x50,0x40,0x3f,0x55,0x33,0x37,0x25,0x77,0x24,0x26,0x74,0x6a,0x28,0x53,0x4d,0x69,0x22,0x5c,0x44,0x31,
		0x36,0x58,0x3b,0x7a,0x51,0x5f,0x52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

	};

	QByteArray ba = in.toLocal8Bit();
	uchar* ascii = (uchar*)ba.constData();
	for (int i = 0; i < in.length() && i < 16; i++)
	{
		int p = ascii[i] + i;
		if (p > 126)
		{
			p = 32 + p % 127;
		}
		out.append(sequence[p]);
	}
	return;
}

/// <summary>
/// returns a QByteArray of a null terminated string
/// </summary>
/// <param name="c"></param>
/// <param name="s"></param>
/// <returns></returns>
static inline QByteArray parseNullTerminatedString(QByteArray c, int s)
{
	//QString res = "";
	QByteArray res;
	for (int i = s; i < c.length(); i++)
	{
		if (c[i] != '\0')
		{
			res.append(c[i]);
		}
		else
		{
			break;
		}
	}
	return res;
}

#endif
