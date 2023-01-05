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
#include <QUuid>

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "packettypes.h"
#include "audiohandler.h"
#include "udpbase.h"
#include "udpcivdata.h"
#include "udpaudio.h"

#define audioLevelBufferSize (4)

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
	void setCurrentRadio(quint8 radio);
    void getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);

signals:
	void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
	void haveAudioData(audioPacket data); // emit this when we have data, connect to rigcommander
	void haveNetworkError(errorType);
	void haveChangeLatency(quint16 value);
	void haveSetVolume(unsigned char value);
	void haveNetworkStatus(networkStatus);
    void haveNetworkAudioLevels(networkAudioLevels);
	void haveBaudRate(quint32 baudrate);
	void requestRadioSelection(QList<radio_cap_packet> radios);
	void setRadioUsage(quint8, quint8 busy, QString name, QString mac);
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

	quint16 civLocalPort;
	quint16 audioLocalPort;

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
	quint8 guid[GUIDLEN];
	bool useGuid = false;
	QByteArray usernameEncoded;
	QByteArray passwordEncoded;

	QTimer* tokenTimer = Q_NULLPTR;
	QTimer* areYouThereTimer = Q_NULLPTR;

	bool highBandwidthConnection = false;
	quint8 civId = 0;
	quint16 rxSampleRates = 0;
	quint16 txSampleRates = 0;
	networkStatus status;
	bool splitWf = false;

    unsigned char audioLevelsTxPeak[audioLevelBufferSize];
    unsigned char audioLevelsRxPeak[audioLevelBufferSize];

    unsigned char audioLevelsTxRMS[audioLevelBufferSize];
    unsigned char audioLevelsRxRMS[audioLevelBufferSize];

    unsigned char audioLevelsTxPosition = 0;
    unsigned char audioLevelsRxPosition = 0;
    unsigned char findMean(unsigned char *d);
    unsigned char findMax(unsigned char *d);


};


#endif
