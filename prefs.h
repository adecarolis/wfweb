#ifndef PREFS_H
#define PREFS_H

#include <QString>

#include "wfviewtypes.h"

struct preferences {
    QString version;
    bool useFullScreen;
    bool useSystemTheme;
    bool drawPeaks;
    underlay_t underlayMode = underlayNone;
    int underlayBufferSize = 64;
    bool wfAntiAlias;
    bool wfInterpolate;
    QString stylesheetPath;
    unsigned char radioCIVAddr;
    bool CIVisRadioModel;
    bool forceRTSasPTT;
    QString serialPortRadio;
    quint32 serialPortBaud;
    int polling_ms;
    bool enablePTT;
    bool niceTS;
    bool enableLAN;
    bool enableRigCtlD;
    quint16 rigCtlPort;
    int currentColorPresetNumber = 0;
    QString virtualSerialPort;
    unsigned char localAFgain;
    unsigned int wflength;
    int wftheme;
    int plotFloor;
    int plotCeiling;
    bool confirmExit;
    bool confirmPowerOff;
    meterKind meter2Type;
    quint16 tcpPort;
    quint8 waterfallFormat;
    audioType audioSystem;
    bool clusterUdpEnable;
    bool clusterTcpEnable;
    int clusterUdpPort;
    QString clusterTcpServerName;
    QString clusterTcpUserName;
    QString clusterTcpPassword;
    int clusterTimeout;
    bool clickDragTuningEnable;
    bool clusterSkimmerSpotsEnable;
};

#endif // PREFS_H
