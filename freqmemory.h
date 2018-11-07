#ifndef FREQMEMORY_H
#define FREQMEMORY_H
#include <QString>
#include <QDebug>

//          0      1        2         3       4
//modes << "LSB" << "USB" << "AM" << "CW" << "RTTY";
//          5      6          7           8          9
// modes << "FM" << "CW-R" << "RTTY-R" << "LSB-D" << "USB-D";

enum mode_kind {
    modeLSB=0,
    modeUSB,
    modeAM,
    modeCW,
    modeRTTY,
    modeFM,
    modeCW_R,
    modeRTTY_R,
    modeLSB_D,
    modeUSB_D
};

struct preset_kind {
    QString name;
    QString comment;
    unsigned int index; // channel number
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

    preset_kind getPreset(unsigned int index);

private:
    void initializePresets();
    unsigned int maxIndex;
};

#endif // FREQMEMORY_H
