#ifndef RIGSERVER_H
#define RIGSERVER_H

#include <QObject>

#include <QDebug>

#include "packettypes.h"
#include "rigidentities.h"
#include "rigcommander.h"

struct SERVERUSER {
	QString username;
	QString password;
	quint8 userType;
};


struct RIGCONFIG {
	QString serialPort;
	quint32 baudRate;
    quint16 civAddr;
	bool civIsRadioModel;
	bool hasWiFi = false;
	bool hasEthernet=false;
    pttType_t pttType=pttCIV;
	audioSetup rxAudioSetup;
	audioSetup txAudioSetup;
	QString modelName;
	QString rigName;
#pragma pack(push, 1)
	union {
		struct {
			quint8 unused[7];        // 0x22
			quint16 commoncap;      // 0x27
			quint8 unusedb;           // 0x29
			quint8 macaddress[6];     // 0x2a
        };
		quint8 guid[GUIDLEN];                  // 0x20
	};
#pragma pack(pop)
	bool rigAvailable=false;
    rigCapabilities* rigCaps = Q_NULLPTR;
	rigCommander* rig = Q_NULLPTR;
	QThread* rigThread = Q_NULLPTR;
	audioHandler* rxaudio = Q_NULLPTR;
	QThread* rxAudioThread = Q_NULLPTR;
	audioHandler* txaudio = Q_NULLPTR;
	QThread* txAudioThread = Q_NULLPTR;
	QTimer* rxAudioTimer = Q_NULLPTR;
	QTimer* connectTimer = Q_NULLPTR;
	quint8 waterfallFormat;
     quint64 queueInterval=1000;
};


struct SERVERCONFIG {
	bool enabled;
    bool disableUI;
	bool lan;
	quint16 controlPort;
	quint16 civPort;
	quint16 audioPort;
	int audioOutput;
	int audioInput;
	quint8 resampleQuality;
	quint32 baudRate;
	QList <SERVERUSER> users;
	QList <RIGCONFIG*> rigs;
};

enum prefServerItem {
    s_enabled = 1 << 0,
    s_disableui = 1 << 1,
    s_lan = 1 << 2,
    s_controlPort = 1 << 3,
    s_civPort = 1 << 4,
    s_audioPort = 1 << 5,
    s_audioOutput = 1 << 6,
    s_audioInput = 1 << 7,
    s_resampleQuality = 1 << 8,
    s_baudRate = 1 << 9,
    s_users = 1 << 10,
    s_rigs = 1 << 11,
    s_all = 1 << 12
};


class rigServer : public QObject
{
	Q_OBJECT

public:
    explicit rigServer(SERVERCONFIG* config, QObject* parent = nullptr);
    ~rigServer();

public slots:
    virtual void init();
    virtual void dataForServer(QByteArray);
    virtual void receiveAudioData(const audioPacket &data);
    virtual void receiveRigCaps(rigCapabilities* caps);

signals:
	void haveDataFromServer(QByteArray);
	void haveAudioData(audioPacket data);
	void haveNetworkStatus(networkStatus);

	void setupTxAudio(audioSetup);
	void setupRxAudio(audioSetup);

protected:
	SERVERCONFIG *config;
    cachingQueue *queue;

private:
    // rigServer should have no private vars as it is only ever subclassed.

};


#endif // RIGSERVER_H
