#ifndef UDPAUDIO_H
#define UDPAUDIO_H


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

#include "audiohandler.h"
#include "pahandler.h"
#include "rthandler.h"


// Class for all audio communications.
class udpAudio : public udpBase
{
	Q_OBJECT

public:
	udpAudio(QHostAddress local, QHostAddress ip, quint16 aport, quint16 lport, audioSetup rxSetup, audioSetup txSetup);
	~udpAudio();

	int audioLatency = 0;

signals:
	void haveAudioData(audioPacket data);

	void setupTxAudio(audioSetup setup);
	void setupRxAudio(audioSetup setup);

	void haveChangeLatency(quint16 value);
	void haveSetVolume(unsigned char value);
    void haveRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void haveTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);

public slots:
	void changeLatency(quint16 value);
	void setVolume(unsigned char value);
    void getRxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void getTxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
	void receiveAudioData(audioPacket audio);

private:

	void sendTxAudio();
	void dataReceived();
	void watchdog();
	void startAudio();
	audioSetup rxSetup;
	audioSetup txSetup;

	uint16_t sendAudioSeq = 0;

	audioHandler* rxaudio = Q_NULLPTR;
	QThread* rxAudioThread = Q_NULLPTR;

	audioHandler* txaudio = Q_NULLPTR;
	QThread* txAudioThread = Q_NULLPTR;

	QTimer* txAudioTimer = Q_NULLPTR;
	bool enableTx = true;

	QMutex audioMutex;

};

#endif
