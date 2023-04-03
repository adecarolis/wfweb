#ifndef PREFS_H
#define PREFS_H

#include <QString>
#include <QColor>
#include <QMap>
#include "wfviewtypes.h"


struct preferences {
    // Program:
    QString version;
    int majorVersion = 0;
    int minorVersion = 0;
    QString gitShort;

    // Interface:
    bool useFullScreen;
    bool useSystemTheme;
    int wfEnable;
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

    // LAN:
    bool enableLAN;
    bool enableRigCtlD;
    quint16 rigCtlPort;
    int currentColorPresetNumber = 0;
    quint16 tcpPort;
    quint8 waterfallFormat;

    // Cluster:
    bool clusterUdpEnable;
    bool clusterTcpEnable;
    int clusterUdpPort;
    QString clusterTcpServerName;
    QString clusterTcpUserName;
    QString clusterTcpPassword;
    int clusterTimeout; 
    bool clusterSkimmerSpotsEnable; 

};

#endif // PREFS_H
