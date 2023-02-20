#ifndef PREFS_H
#define PREFS_H

#include <QString>

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
    if_meter2Type = 1 << 14,
    if_clickDragTuningEnable = 1 << 15,
    if_currentColorPresetNumber = 1 << 16,
    if_all = 1 << 17
};

enum prefRaItem {

    ra_radioCIVAddr = 1 << 0,
    ra_CIVisRadioModel = 1 << 1,
    ra_forceRTSasPTT = 1 << 2,
    ra_polling_ms = 1 << 3,
    ra_serialPortRadio = 1 << 4,
    ra_serialPortBaud = 1 << 5,
    ra_virtualSerialPort = 1 << 6,
    ra_localAFgain = 1 << 7,
    ra_audioSystem = 1 << 8,
    ra_all = 1 << 9
};

enum prefCtItem {
    ct_enablePTT = 1 << 0,
    ct_niceTS = 1 << 1,
    ct_automaticSidebandSwitching = 1 << 2,
    ct_enableUSBControllers = 1 << 3,
    ct_usbSensitivity = 1 << 4,
    ct_all = 1 << 5
};

enum prefLanItem {

    l_enableLAN = 1 << 0,
    l_enableRigCtlD = 1 << 1,
    l_rigCtlPort = 1 << 2,
    l_tcpPort = 1 << 3,
    l_waterfallFormat = 1 << 4,
    l_all = 1 << 5
};

enum prefClusterItem {
    cl_clusterUdpEnable = 1 << 0,
    cl_clusterTcpEnable = 1 << 1,
    cl_clusterUdpPort = 1 << 2,
    cl_clusterTcpServerName = 1 << 3,
    cl_clusterTcpUserName = 1 << 4,
    cl_clusterTcpPassword = 1 << 5,
    cl_clusterTimeout = 1 << 6,
    cl_clusterSkimmerSpotsEnable = 1 << 7,
    cl_all = 1 << 8
};



struct preferences {
    // Program:
    QString version;
    int majorVersion = 0;
    int minorVersion = 0;
    QString gitShort;

    // Interface:
    bool useFullScreen;
    bool useSystemTheme;
    bool drawPeaks;
    underlay_t underlayMode = underlayNone;
    int underlayBufferSize = 64;
    bool wfAntiAlias;
    bool wfInterpolate;
    int wftheme;
    int plotFloor;
    int plotCeiling;
    QString stylesheetPath;
    unsigned int wflength;
    bool confirmExit;
    bool confirmPowerOff;
    meterKind meter2Type;
    bool clickDragTuningEnable;
    int currentColorPresetNumber = 0;


    // Radio:
    unsigned char radioCIVAddr;
    bool CIVisRadioModel;
    bool forceRTSasPTT;
    int polling_ms;
    QString serialPortRadio;
    quint32 serialPortBaud;
    QString virtualSerialPort;
    unsigned char localAFgain;
    audioType audioSystem;

    // Controls:
    bool enablePTT;
    bool niceTS;
    bool automaticSidebandSwitching = true;
    bool enableUSBControllers;
    int usbSensitivity;

    // LAN:
    bool enableLAN;
    bool enableRigCtlD;
    quint16 rigCtlPort;
    quint16 tcpPort;
    quint8 waterfallFormat;

    // Cluster:
    bool clusterUdpEnable;
    bool clusterTcpEnable;
    int clusterUdpPort;
    QString clusterTcpServerName;
    QString clusterTcpUserName;
    QString clusterTcpPassword;
    int clusterTimeout; // used?
    bool clusterSkimmerSpotsEnable; // where is this used?
};

#endif // PREFS_H
