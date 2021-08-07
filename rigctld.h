/* This file contains portions of the Hamlib Interface - API header
* Copyright(c) 2000 - 2003 by Frank Singleton
* Copyright(c) 2000 - 2012 by Stephane Fillod 
*/


#ifndef RIGCTLD_H
#define RIGCTLD_H

#include <QObject>
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSet>
#include <QDataStream>

#include <map>
#include <vector>
#include <typeindex>

#include "rigcommander.h"

#define CONSTANT_64BIT_FLAG(BIT) (1ull << (BIT))

#define    RIG_MODE_NONE      0                         /*!< '' -- None */
#define    RIG_MODE_AM        CONSTANT_64BIT_FLAG (0)   /*!< \c AM -- Amplitude Modulation */
#define    RIG_MODE_CW        CONSTANT_64BIT_FLAG (1)   /*!< \c CW -- CW "normal" sideband */
#define    RIG_MODE_USB       CONSTANT_64BIT_FLAG (2)   /*!< \c USB -- Upper Side Band */
#define    RIG_MODE_LSB       CONSTANT_64BIT_FLAG (3)   /*!< \c LSB -- Lower Side Band */
#define    RIG_MODE_RTTY      CONSTANT_64BIT_FLAG (4)   /*!< \c RTTY -- Radio Teletype */
#define    RIG_MODE_FM        CONSTANT_64BIT_FLAG (5)   /*!< \c FM -- "narrow" band FM */
#define    RIG_MODE_WFM       CONSTANT_64BIT_FLAG (6)   /*!< \c WFM -- broadcast wide FM */
#define    RIG_MODE_CWR       CONSTANT_64BIT_FLAG (7)   /*!< \c CWR -- CW "reverse" sideband */
#define    RIG_MODE_RTTYR     CONSTANT_64BIT_FLAG (8)   /*!< \c RTTYR -- RTTY "reverse" sideband */
#define    RIG_MODE_AMS       CONSTANT_64BIT_FLAG (9)   /*!< \c AMS -- Amplitude Modulation Synchronous */
#define    RIG_MODE_PKTLSB    CONSTANT_64BIT_FLAG (10)  /*!< \c PKTLSB -- Packet/Digital LSB mode (dedicated port) */
#define    RIG_MODE_PKTUSB    CONSTANT_64BIT_FLAG (11)  /*!< \c PKTUSB -- Packet/Digital USB mode (dedicated port) */
#define    RIG_MODE_PKTFM     CONSTANT_64BIT_FLAG (12)  /*!< \c PKTFM -- Packet/Digital FM mode (dedicated port) */
#define    RIG_MODE_ECSSUSB   CONSTANT_64BIT_FLAG (13)  /*!< \c ECSSUSB -- Exalted Carrier Single Sideband USB */
#define    RIG_MODE_ECSSLSB   CONSTANT_64BIT_FLAG (14)  /*!< \c ECSSLSB -- Exalted Carrier Single Sideband LSB */
#define    RIG_MODE_FAX       CONSTANT_64BIT_FLAG (15)  /*!< \c FAX -- Facsimile Mode */
#define    RIG_MODE_SAM       CONSTANT_64BIT_FLAG (16)  /*!< \c SAM -- Synchronous AM double sideband */
#define    RIG_MODE_SAL       CONSTANT_64BIT_FLAG (17)  /*!< \c SAL -- Synchronous AM lower sideband */
#define    RIG_MODE_SAH       CONSTANT_64BIT_FLAG (18)  /*!< \c SAH -- Synchronous AM upper (higher) sideband */
#define    RIG_MODE_DSB       CONSTANT_64BIT_FLAG (19)  /*!< \c DSB -- Double sideband suppressed carrier */
#define    RIG_MODE_FMN       CONSTANT_64BIT_FLAG (21)  /*!< \c FMN -- FM Narrow Kenwood ts990s */
#define    RIG_MODE_PKTAM     CONSTANT_64BIT_FLAG (22)  /*!< \c PKTAM -- Packet/Digital AM mode e.g. IC7300 */
#define    RIG_MODE_P25       CONSTANT_64BIT_FLAG (23)  /*!< \c P25 -- APCO/P25 VHF,UHF digital mode IC-R8600 */
#define    RIG_MODE_DSTAR     CONSTANT_64BIT_FLAG (24)  /*!< \c D-Star -- VHF,UHF digital mode IC-R8600 */
#define    RIG_MODE_DPMR      CONSTANT_64BIT_FLAG (25)  /*!< \c dPMR -- digital PMR, VHF,UHF digital mode IC-R8600 */
#define    RIG_MODE_NXDNVN    CONSTANT_64BIT_FLAG (26)  /*!< \c NXDN-VN -- VHF,UHF digital mode IC-R8600 */
#define    RIG_MODE_NXDN_N    CONSTANT_64BIT_FLAG (27)  /*!< \c NXDN-N -- VHF,UHF digital mode IC-R8600 */
#define    RIG_MODE_DCR       CONSTANT_64BIT_FLAG (28)  /*!< \c DCR -- VHF,UHF digital mode IC-R8600 */
#define    RIG_MODE_AMN       CONSTANT_64BIT_FLAG (29)  /*!< \c AM-N -- Narrow band AM mode IC-R30 */
#define    RIG_MODE_PSK       CONSTANT_64BIT_FLAG (30)  /*!< \c PSK - Kenwood PSK and others */
#define    RIG_MODE_PSKR      CONSTANT_64BIT_FLAG (31)  /*!< \c PSKR - Kenwood PSKR and others */
#define    RIG_MODE_DD        CONSTANT_64BIT_FLAG (32)  /*!< \c DD Mode IC-9700 */
#define    RIG_MODE_C4FM      CONSTANT_64BIT_FLAG (33)  /*!< \c Yaesu C4FM mode */
#define    RIG_MODE_PKTFMN    CONSTANT_64BIT_FLAG (34)  /*!< \c Yaesu DATA-FM-N */
#define    RIG_MODE_SPEC      CONSTANT_64BIT_FLAG (35)  /*!< \c Unfiltered as in PowerSDR */

static struct
{
    quint64 mode;
    const char* str;
} mode_str[] =
{
    { RIG_MODE_AM, "AM" },
    { RIG_MODE_CW, "CW" },
    { RIG_MODE_USB, "USB" },
    { RIG_MODE_LSB, "LSB" },
    { RIG_MODE_RTTY, "RTTY" },
    { RIG_MODE_FM, "FM" },
    { RIG_MODE_WFM, "WFM" },
    { RIG_MODE_CWR, "CWR" },
    { RIG_MODE_RTTYR, "RTTYR" },
    { RIG_MODE_AMS, "AMS" },
    { RIG_MODE_PKTLSB, "PKTLSB" },
    { RIG_MODE_PKTUSB, "PKTUSB" },
    { RIG_MODE_PKTFM, "PKTFM" },
    { RIG_MODE_PKTFMN, "PKTFMN" },
    { RIG_MODE_ECSSUSB, "ECSSUSB" },
    { RIG_MODE_ECSSLSB, "ECSSLSB" },
    { RIG_MODE_FAX, "FAX" },
    { RIG_MODE_SAM, "SAM" },
    { RIG_MODE_SAL, "SAL" },
    { RIG_MODE_SAH, "SAH" },
    { RIG_MODE_DSB, "DSB"},
    { RIG_MODE_FMN, "FMN" },
    { RIG_MODE_PKTAM, "PKTAM"},
    { RIG_MODE_P25, "P25"},
    { RIG_MODE_DSTAR, "D-STAR"},
    { RIG_MODE_DPMR, "DPMR"},
    { RIG_MODE_NXDNVN, "NXDN-VN"},
    { RIG_MODE_NXDN_N, "NXDN-N"},
    { RIG_MODE_DCR, "DCR"},
    { RIG_MODE_AMN, "AMN"},
    { RIG_MODE_PSK, "PSK"},
    { RIG_MODE_PSKR, "PSKR"},
    { RIG_MODE_C4FM, "C4FM"},
    { RIG_MODE_SPEC, "SPEC"},
    { RIG_MODE_NONE, "" },
};


class rigCtlD : public QTcpServer
{
    Q_OBJECT

public:
    explicit rigCtlD(QObject *parent=Q_NULLPTR);
    virtual ~rigCtlD();

    int startServer(qint16 port);
    void stopServer();
    rigCapabilities rigCaps;

signals:
    void onStarted();
    void onStopped();
    void sendData(QString data);
    void setFrequency(freqt freq);
    void setPTT(bool state);
    void setMode(unsigned char mode, unsigned char modeFilter);
    void setDataMode(bool dataOn, unsigned char modeFilter);
    void setVFO(unsigned char vfo);
    void setSplit(unsigned char split);
    void setDuplexMode(duplexMode dm);

    // Power
    void sendPowerOn();
    void sendPowerOff();

    // Att/preamp
    void setAttenuator(unsigned char att);
    void setPreamp(unsigned char pre);

    //Level set
    void setRfGain(unsigned char level);
    void setAfGain(unsigned char level);
    void setSql(unsigned char level);
    void setMicGain(unsigned char);
    void setCompLevel(unsigned char);
    void setTxPower(unsigned char);
    void setMonitorLevel(unsigned char);
    void setVoxGain(unsigned char);
    void setAntiVoxGain(unsigned char);
    void setSpectrumRefLevel(int);


public slots:
    virtual void incomingConnection(qintptr socketDescriptor);
    void receiveRigCaps(rigCapabilities caps);
    void receiveStateInfo(rigStateStruct* state);
    void receiveFrequency(freqt freq);

private: 
    rigStateStruct* rigState = Q_NULLPTR;
};


class rigCtlClient : public QObject
{
        Q_OBJECT

public:

    explicit rigCtlClient(int socket, rigCapabilities caps, rigStateStruct *state, rigCtlD* parent = Q_NULLPTR);
    int getSocketId();


public slots:
    void socketReadyRead(); 
    void socketDisconnected();
    void closeSocket();
    void sendData(QString data);

protected:
    int sessionId;
    QTcpSocket* socket = Q_NULLPTR;
    QString commandBuffer;

private:
    rigCapabilities rigCaps;
    rigStateStruct* rigState = Q_NULLPTR;
    rigCtlD* parent;
    QString getMode(unsigned char mode, bool datamode);
    unsigned char getMode(QString modeString);
    QString getFilter(unsigned char mode, unsigned char filter);
    QString generateFreqRange(bandType band);
    unsigned char getAntennas();
    quint64 getRadioModes();
    QString getAntName(unsigned char ant);
};


#endif
