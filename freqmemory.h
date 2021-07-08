#ifndef FREQMEMORY_H
#define FREQMEMORY_H
#include <QString>
#include <QDebug>

//          0      1        2         3       4
//modes << "LSB" << "USB" << "AM" << "CW" << "RTTY";
//          5      6          7           8          9
// modes << "FM" << "CW-R" << "RTTY-R" << "LSB-D" << "USB-D";

enum mode_kind {
    modeLSB=0x00,
    modeUSB=0x01,
    modeAM=0x02,
    modeCW=0x03,
    modeRTTY=0x04,
    modeFM=0x05,
    modeCW_R=0x07,
    modeRTTY_R=0x08,
    modeLSB_D=0x80,
    modeUSB_D=0x81,
    modeDV=0x17,
    modeDD=0x27,
    modeWFM,
    modeS_AMD,
    modeS_AML,
    modeS_AMU,
    modeP25,
    modedPMR,
    modeNXDN_VN,
    modeNXDN_N,
    modeDCR,
    modePSK,
    modePSK_R
};

struct mode_info {
    mode_kind mk;
    unsigned char reg;
    unsigned char filter;
    QString name;
};

struct preset_kind {
    // QString name;
    // QString comment;
    // unsigned int index; // channel number
    double frequency;
    mode_kind mode;
    bool isSet;
};

class freqMemory
{
public:
    freqMemory();
    void setPreset(unsigned int index, double frequency, mode_kind mode);
    void setPreset(unsigned int index, double frequency, mode_kind mode, QString name);
    void setPreset(unsigned int index, double frequency, mode_kind mode, QString name, QString comment);
    void dumpMemory();
    unsigned int getNumPresets();
    preset_kind getPreset(unsigned int index);

private:
    void initializePresets();
    unsigned int numPresets;
    unsigned int maxIndex;
    //QVector <preset_kind> presets;
    preset_kind presets[100];

};

#endif // FREQMEMORY_H
