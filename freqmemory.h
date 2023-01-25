#ifndef FREQMEMORY_H
#define FREQMEMORY_H
#include <QString>
#include <QDebug>
#include "wfviewtypes.h"

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
