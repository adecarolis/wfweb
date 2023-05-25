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
#include <QHash>

#include <map>
#include <vector>
#include <typeindex>

#include "rigcommander.h"
#include "cachingqueue.h"
#include "rigstate.h"

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

struct commandStruct
{
    uchar sstr;
    const char *str;
    funcs func;
    char type;
    int flags = 0;
    const char* arg1=nullptr;
    const char* arg2=nullptr;
    const char* arg3=nullptr;
    const char* arg4=nullptr;
    const char* arg5=nullptr;
    const char* arg6=nullptr;
};

struct subCommandStruct
{
    const char *str;
    funcs func;
    char type;
};


enum rig_errcode_e {
    RIG_OK=0,		/*!< No error, operation completed successfully */
    RIG_EINVAL,		/*!< invalid parameter */
    RIG_ECONF,		/*!< invalid configuration (serial,..) */
    RIG_ENOMEM,		/*!< memory shortage */
    RIG_ENIMPL,		/*!< function not implemented, but will be */
    RIG_ETIMEOUT,		/*!< communication timed out */
    RIG_EIO,		/*!< IO error, including open failed */
    RIG_EINTERNAL,		/*!< Internal Hamlib error, huh! */
    RIG_EPROTO,		/*!< Protocol error */
    RIG_ERJCTED,		/*!< Command rejected by the rig */
    RIG_ETRUNC,		/*!< Command performed, but arg truncated */
    RIG_ENAVAIL,		/*!< function not available */
    RIG_ENTARGET,		/*!< VFO not targetable */
    RIG_BUSERROR,		/*!< Error talking on the bus */
    RIG_BUSBUSY,		/*!< Collision on the bus */
    RIG_EARG,		/*!< NULL RIG handle or any invalid pointer parameter in get arg */
    RIG_EVFO,		/*!< Invalid VFO */
    RIG_EDOM		/*!< Argument out of domain of func */
};


#if 0

static struct
{
    quint64 func;
    const char* str;
} rig_func_str[] =
{
    { RIG_FUNC_FAGC, "FAGC" },
    { RIG_FUNC_NB, "NB" },
    { RIG_FUNC_COMP, "COMP" },
    { RIG_FUNC_VOX, "VOX" },
    { RIG_FUNC_TONE, "TONE" },
    { RIG_FUNC_TSQL, "TSQL" },
    { RIG_FUNC_SBKIN, "SBKIN" },
    { RIG_FUNC_FBKIN, "FBKIN" },
    { RIG_FUNC_ANF, "ANF" },
    { RIG_FUNC_NR, "NR" },
    { RIG_FUNC_AIP, "AIP" },
    { RIG_FUNC_APF, "APF" },
    { RIG_FUNC_MON, "MON" },
    { RIG_FUNC_MN, "MN" },
    { RIG_FUNC_RF, "RF" },
    { RIG_FUNC_ARO, "ARO" },
    { RIG_FUNC_LOCK, "LOCK" },
    { RIG_FUNC_MUTE, "MUTE" },
    { RIG_FUNC_VSC, "VSC" },
    { RIG_FUNC_REV, "REV" },
    { RIG_FUNC_SQL, "SQL" },
    { RIG_FUNC_ABM, "ABM" },
    { RIG_FUNC_BC, "BC" },
    { RIG_FUNC_MBC, "MBC" },
    { RIG_FUNC_RIT, "RIT" },
    { RIG_FUNC_AFC, "AFC" },
    { RIG_FUNC_SATMODE, "SATMODE" },
    { RIG_FUNC_SCOPE, "SCOPE" },
    { RIG_FUNC_RESUME, "RESUME" },
    { RIG_FUNC_TBURST, "TBURST" },
    { RIG_FUNC_TUNER, "TUNER" },
    { RIG_FUNC_XIT, "XIT" },
    { RIG_FUNC_NB2, "NB2" },
    { RIG_FUNC_DSQL, "DSQL" },
    { RIG_FUNC_AFLT, "AFLT" },
    { RIG_FUNC_ANL, "ANL" },
    { RIG_FUNC_BC2, "BC2" },
    { RIG_FUNC_DUAL_WATCH, "DUAL_WATCH"},
    { RIG_FUNC_DIVERSITY, "DIVERSITY"},
    { RIG_FUNC_CSQL, "CSQL" },
    { RIG_FUNC_SCEN, "SCEN" },
    { RIG_FUNC_TRANSCEIVE, "TRANSCEIVE" },
    { RIG_FUNC_SPECTRUM, "SPECTRUM" },
    { RIG_FUNC_SPECTRUM_HOLD, "SPECTRUM_HOLD" },
    { RIG_FUNC_NONE, "" },
};

static struct
{
    quint64 level;
    const char* str;
} rig_level_str[] =
{
    { RIG_LEVEL_PREAMP, "PREAMP" },
    { RIG_LEVEL_ATT, "ATT" },
    { RIG_LEVEL_VOXDELAY, "VOXDELAY" },
    { RIG_LEVEL_AF, "AF" },
    { RIG_LEVEL_RF, "RF" },
    { RIG_LEVEL_SQL, "SQL" },
    { RIG_LEVEL_IF, "IF" },
    { RIG_LEVEL_APF, "APF" },
    { RIG_LEVEL_NR, "NR" },
    { RIG_LEVEL_PBT_IN, "PBT_IN" },
    { RIG_LEVEL_PBT_OUT, "PBT_OUT" },
    { RIG_LEVEL_CWPITCH, "CWPITCH" },
    { RIG_LEVEL_RFPOWER, "RFPOWER" },
    { RIG_LEVEL_MICGAIN, "MICGAIN" },
    { RIG_LEVEL_KEYSPD, "KEYSPD" },
    { RIG_LEVEL_NOTCHF, "NOTCHF" },
    { RIG_LEVEL_COMP, "COMP" },
    { RIG_LEVEL_AGC, "AGC" },
    { RIG_LEVEL_BKINDL, "BKINDL" },
    { RIG_LEVEL_BALANCE, "BAL" },
    { RIG_LEVEL_METER, "METER" },
    { RIG_LEVEL_VOXGAIN, "VOXGAIN" },
    { RIG_LEVEL_ANTIVOX, "ANTIVOX" },
    { RIG_LEVEL_SLOPE_LOW, "SLOPE_LOW" },
    { RIG_LEVEL_SLOPE_HIGH, "SLOPE_HIGH" },
    { RIG_LEVEL_BKIN_DLYMS, "BKIN_DLYMS" },
    { RIG_LEVEL_RAWSTR, "RAWSTR" },
    { RIG_LEVEL_SWR, "SWR" },
    { RIG_LEVEL_ALC, "ALC" },
    { RIG_LEVEL_STRENGTH, "STRENGTH" },
    { RIG_LEVEL_RFPOWER_METER, "RFPOWER_METER" },
    { RIG_LEVEL_COMP_METER, "COMP_METER" },
    { RIG_LEVEL_VD_METER, "VD_METER" },
    { RIG_LEVEL_ID_METER, "ID_METER" },
    { RIG_LEVEL_NOTCHF_RAW, "NOTCHF_RAW" },
    { RIG_LEVEL_MONITOR_GAIN, "MONITOR_GAIN" },
    { RIG_LEVEL_NB, "NB" },
    { RIG_LEVEL_RFPOWER_METER_WATTS, "RFPOWER_METER_WATTS" },
    { RIG_LEVEL_SPECTRUM_MODE, "SPECTRUM_MODE" },
    { RIG_LEVEL_SPECTRUM_SPAN, "SPECTRUM_SPAN" },
    { RIG_LEVEL_SPECTRUM_EDGE_LOW, "SPECTRUM_EDGE_LOW" },
    { RIG_LEVEL_SPECTRUM_EDGE_HIGH, "SPECTRUM_EDGE_HIGH" },
    { RIG_LEVEL_SPECTRUM_SPEED, "SPECTRUM_SPEED" },
    { RIG_LEVEL_SPECTRUM_REF, "SPECTRUM_REF" },
    { RIG_LEVEL_SPECTRUM_AVG, "SPECTRUM_AVG" },
    { RIG_LEVEL_SPECTRUM_ATT, "SPECTRUM_ATT" },
    { RIG_LEVEL_TEMP_METER, "TEMP_METER" },
    { RIG_LEVEL_NONE, "" },
};

#endif

struct cal_table {
    int size;                   /*!< number of plots in the table */
    struct {
        int raw;                /*!< raw (A/D) value, as returned by \a RIG_LEVEL_RAWSTR */
        int val;                /*!< associated value, basically the measured dB value */
    } table[32];    /*!< table of plots */
};

typedef struct cal_table cal_table_t;

#define IC7610_STR_CAL { 16, \
    { \
        {   0, -54 }, /* S0 */ \
        {  11, -48 }, \
        {  21, -42 }, \
        {  34, -36 }, \
        {  50, -30 }, \
        {  59, -24 }, \
        {  75, -18 }, \
        {  93, -12 }, \
        { 103,  -6 }, \
        { 124,   0 }, /* S9 */ \
        { 145,  10 }, \
        { 160,  20 }, \
        { 183,  30 }, \
        { 204,  40 }, \
        { 222,  50 }, \
        { 246,  60 } /* S9+60dB */  \
    } }

#define IC7850_STR_CAL { 3, \
    { \
        {   0, -54 }, /* S0 */ \
        { 120,   0 }, /* S9 */ \
        { 241,  60 }  /* S9+60 */ \
    } }

#define IC7300_STR_CAL { 7, \
    { \
        {   0, -54 }, \
        {  10, -48 }, \
        {  30, -36 }, \
        {  60, -24 }, \
        {  90, -12 }, \
        { 120,  0 }, \
        { 241,  64 } \
    } }

class rigCtlD : public QTcpServer
{
    Q_OBJECT

public:
    explicit rigCtlD(QObject *parent=nullptr);
    virtual ~rigCtlD();

    int startServer(qint16 port);
    void stopServer();
    rigCapabilities rigCaps;

signals:
    void onStarted();
    void onStopped();
    void sendData(QString data);
    void setFrequency(quint8 vfo, freqt freq);
    void setPTT(bool state);
    void setMode(quint8 mode, quint8 modeFilter);
    void setDataMode(bool dataOn, quint8 modeFilter);
    void setVFO(quint8 vfo);
    void setSplit(quint8 split);
    void setDuplexMode(duplexMode_t dm);
    void stateUpdated();
    // Power
    void sendPowerOn();
    void sendPowerOff();

    // Att/preamp
    void setAttenuator(quint8 att);
    void setPreamp(quint8 pre);

    //Level set
    void setRfGain(quint8 level);
    void setAfGain(quint8 level);
    void setSql(quint8 level);
    void setMicGain(quint8);
    void setCompLevel(quint8);
    void setTxPower(quint8);
    void setMonitorGain(quint8);
    void setVoxGain(quint8);
    void setAntiVoxGain(quint8);
    void setSpectrumRefLevel(int);

public slots:
    virtual void incomingConnection(qintptr socketDescriptor);
    void receiveRigCaps(rigCapabilities caps);
    void receiveStateInfo(rigstate* state);
//    void receiveFrequency(freqt freq);

private: 
    rigstate* rigState = Q_NULLPTR;

};


class rigCtlClient : public QObject
{
        Q_OBJECT

public:

    explicit rigCtlClient(int socket, rigCapabilities caps, rigstate *state, rigCtlD* parent = Q_NULLPTR);
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
    cachingQueue* queue;
    rigCapabilities rigCaps;
    rigstate* rigState = Q_NULLPTR;
    rigCtlD* parent;
    bool chkVfoEecuted=false;
    unsigned long crcTable[256];
    unsigned long doCrc(unsigned char* p, size_t n);
    void genCrc(unsigned long crcTable[]);
    QString getMode(modeInfo mode);
    bool getMode(QString modeString, modeInfo& mode);
    QString getFilter(quint8 mode, quint8 filter);
    quint8 getAntennas();
    quint64 getRadioModes(QString mode = "");
    QString getAntName(quint8 ant);
    quint8 antFromName(QString name);
    quint8 vfoFromName(QString vfo);
    QString getVfoName(quint8 vfo);
    int getCommand(QStringList& respone, bool extended, const commandStruct cmd, QStringList params );
    int getSubCommand(QStringList& response, bool extended, const commandStruct cmd, const subCommandStruct sub[], QStringList params);
    int dumpState(QStringList &response, bool extended);
    int dumpCaps(QStringList &response, bool extended);
    int getCalibratedValue(quint8 meter,cal_table_t cal);
};


#endif
