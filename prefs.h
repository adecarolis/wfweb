#ifndef PREFS_H
#define PREFS_H

#include <QString>
#include <QColor>
#include <QMap>
#include "audioconverter.h"
#include "cluster.h"
#include "rigidentities.h"
#include "wfviewtypes.h"

enum prefIfItem {
    if_useFullScreen = 1 << 0,
    if_useSystemTheme = 1 << 1,
    if_drawPeaks = 1 << 2,
    if_underlayMode = 1 << 3,
    if_underlayBufferSize = 1 << 4,
    if_wfAntiAlias = 1 << 5,
    if_wfInterpolate = 1 << 6,
    if_wftheme = 1 << 7,
    if_plotFloor = 1 << 8,
    if_plotCeiling = 1 << 9,
    if_stylesheetPath = 1 << 10,
    if_wflength = 1 << 11,
    if_confirmExit = 1 << 12,
    if_confirmPowerOff = 1 << 13,
    if_meter1Type = 1 << 14,
    if_meter2Type = 1 << 15,
    if_meter3Type = 1 << 16,
    if_compMeterReverse = 1 << 17,
    if_clickDragTuningEnable = 1 << 18,
    if_currentColorPresetNumber = 1 << 19,
    if_rigCreatorEnable = 1 << 20,
    if_frequencyUnits = 1 << 21,
    if_region = 1 << 22,
    if_showBands = 1 << 23,
    if_separators = 1 << 24,
    if_all = 1 << 25
};

enum prefColItem {
    col_grid = 1 << 0,
    col_axis = 1 << 1,
    col_text = 1 << 2,
    col_plotBackground = 1 << 3,
    col_spectrumLine = 1 << 4,
    col_spectrumFill = 1 << 5,
    col_useSpectrumFillGradient = 1 << 6,
    col_spectrumFillTop = 1 << 7,
    col_spectrumFillBot = 1 << 8,
    col_underlayLine = 1 << 9,
    col_underlayFill = 1 << 10,
    col_useUnderlayFillGradient = 1 << 11,
    col_underlayFillTop = 1 << 12,
    col_underlayFillBot = 1 << 13,
    col_tuningLine = 1 << 14,
    col_passband = 1 << 15,
    col_pbtIndicator = 1 << 16,
    col_meterLevel = 1 << 17,
    col_meterAverage = 1 << 18,
    col_meterPeakLevel = 1 << 19,
    col_meterHighScale = 1 << 20,
    col_meterScale = 1 << 21,
    col_meterText = 1 << 22,
    col_waterfallBack = 1 << 23,
    col_waterfallGrid = 1 << 24,
    col_waterfallAxis = 1 << 25,
    col_waterfallText = 1 << 26,
    col_clusterSpots = 1 << 27,
    col_all = 1 << 28
};

enum prefRsItem {
    rs_dataOffMod = 1 << 0,
    rs_data1Mod = 1 << 1,
    rs_data2Mod = 1 << 2,
    rs_data3Mod = 1 << 3,
    rs_setClock = 1 << 4,
    rs_clockUseUtc = 1 << 5,
    rs_pttOn = 1 << 6,
    rs_pttOff = 1 << 7,
    rs_satOps = 1 << 8,
    rs_adjRef = 1 << 9,
    rs_debug = 1 << 10,
    rs_all = 1 << 11
};

enum prefRaItem {

    ra_radioCIVAddr = 1 << 0,
    ra_CIVisRadioModel = 1 << 1,
    ra_forceRTSasPTT = 1 << 2,
    ra_polling_ms = 1 << 3,
    ra_serialEnabled = 1 << 4,
    ra_serialPortRadio = 1 << 5,
    ra_serialPortBaud = 1 << 6,
    ra_virtualSerialPort = 1 << 7,
    ra_localAFgain = 1 << 8,
    ra_audioSystem = 1 << 9,
    ra_all = 1 << 10
};

enum prefCtItem {
    ct_enablePTT = 1 << 0,
    ct_niceTS = 1 << 1,
    ct_automaticSidebandSwitching = 1 << 2,
    ct_enableUSBControllers = 1 << 3,
    ct_USBControllersSetup = 1 << 4,
    ct_USBControllersReset = 1 << 5,
    ct_all = 1 << 6
};

enum prefLanItem {

    l_enableLAN = 1 << 0,
    l_enableRigCtlD = 1 << 1,
    l_rigCtlPort = 1 << 2,
    l_tcpPort = 1 << 3,
    l_tciPort = 1 << 4,
    l_waterfallFormat = 1 << 5,
    l_all = 1 << 6
};

enum prefClusterItem {
    cl_clusterUdpEnable = 1 << 0,
    cl_clusterTcpEnable = 1 << 1,
    cl_clusterUdpPort = 1 << 2,
    cl_clusterTcpServerName = 1 << 3,
    cl_clusterTcpUserName = 1 << 4,
    cl_clusterTcpPassword = 1 << 5,
    cl_clusterTcpPort = 1 << 6,
    cl_clusterTimeout = 1 << 7,
    cl_clusterSkimmerSpotsEnable = 1 << 8,
    cl_clusterTcpConnect = 1 << 9,
    cl_clusterTcpDisconnect = 1 << 10,
    cl_all = 1 << 11
};

enum prefUDPItem {
    u_enabled = 1 << 0,
    u_ipAddress = 1 << 1,
    u_controlLANPort = 1 << 2,
    u_serialLANPort = 1 << 3,
    u_audioLANPort = 1 << 4,
    u_username = 1 << 5,
    u_password = 1 << 6,
    u_clientName = 1 << 7,
    u_waterfallFormat = 1 << 8,
    u_halfDuplex = 1 << 9,
    u_sampleRate = 1 << 10,
    u_rxCodec = 1 << 11,
    u_txCodec = 1 << 12,
    u_rxLatency = 1 << 13,
    u_txLatency = 1 << 14,
    u_audioInput = 1 << 15,
    u_audioOutput = 1 << 16,
    u_all = 1 << 17
};



struct preferences {
    // Program:
    QString version;
    int majorVersion = 0;
    int minorVersion = 0;
    QString gitShort;
    bool hasRunSetup = false;
    bool settingsChanged = false;

    // Interface:
    bool useFullScreen;
    bool useSystemTheme;
    int wfEnable;
    bool drawPeaks;
    underlay_t underlayMode = underlayNone;
    int underlayBufferSize = 64;
    bool wfAntiAlias;
    bool wfInterpolate;
    int mainWfTheme;
    int subWfTheme;
    int mainPlotFloor;
    int subPlotFloor;
    int mainPlotCeiling;
    int subPlotCeiling;
    unsigned int mainWflength;
    unsigned int subWflength;
    int scopeScrollX;
    int scopeScrollY;
    QString stylesheetPath;
    bool confirmExit;
    bool confirmPowerOff;
    bool confirmSettingsChanged;
    meter_t meter1Type;
    meter_t meter2Type;
    meter_t meter3Type;
    bool compMeterReverse = false;
    bool clickDragTuningEnable;
    int currentColorPresetNumber = 0;
    bool rigCreatorEnable = false;
    int frequencyUnits = 3;
    QString region;
    bool showBands;

    // Radio:
    quint8 radioCIVAddr;
    bool CIVisRadioModel;
    bool forceRTSasPTT;
    int polling_ms;
    QString serialPortRadio;
    quint32 serialPortBaud;
    QString virtualSerialPort;
    quint8 localAFgain;
    audioType audioSystem;

    // Controls:
    bool enablePTT;
    bool niceTS;
    bool automaticSidebandSwitching = true;
    bool enableUSBControllers;

    // LAN:
    bool enableLAN;
    bool enableRigCtlD;
    quint16 rigCtlPort;
    quint16 tcpPort;
    quint16 tciPort=50001;
    quint8 waterfallFormat;

    // Cluster:
    QList<clusterSettings> clusters;
    bool clusterUdpEnable;
    bool clusterTcpEnable;
    int clusterUdpPort;
    int clusterTcpPort = 7300;
    int clusterTimeout; // used?
    bool clusterSkimmerSpotsEnable; // where is this used?
    QString clusterTcpServerName;
    QString clusterTcpUserName;
    QString clusterTcpPassword;

    // Temporary settings
    rigInput inputSource[4];
    bool useUTC = false;

    audioSetup rxSetup;
    audioSetup txSetup;

    QChar decimalSeparator;
    QChar groupSeparator;

};

#endif // PREFS_H
