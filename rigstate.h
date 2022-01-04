#ifndef RIGSTATEH
#define RIGSTATEH

#include <QObject>
#include <QMutex>
#include <QDateTime>
#include <QVariant>
#include <QMap>
#include <QCache>

#include "rigcommander.h"
#include "rigidentities.h"

// Meters at the end as they are ALWAYS updated from the rig!
enum stateTypes { VFOAFREQ, VFOBFREQ, CURRENTVFO, PTT, MODE, FILTER, DUPLEX, DATAMODE, ANTENNA, RXANTENNA, CTCSS, TSQL, DTCS, CSQL,
                  PREAMP, AGC, ATTENUATOR, MODINPUT, AFGAIN, RFGAIN, SQUELCH, TXPOWER, MICGAIN, COMPLEVEL, MONITORLEVEL, VOXGAIN, ANTIVOXGAIN,
                  FAGCFUNC, NBFUNC, COMPFUNC, VOXFUNC, TONEFUNC, TSQLFUNC, SBKINFUNC, FBKINFUNC, ANFFUNC, NRFUNC, AIPFUNC, APFFUNC, MONFUNC, MNFUNC,RFFUNC,
                  AROFUNC, MUTEFUNC, VSCFUNC, REVFUNC, SQLFUNC, ABMFUNC, BCFUNC, MBCFUNC, RITFUNC, AFCFUNC, SATMODEFUNC, SCOPEFUNC,
                  NBLEVEL, NBDEPTH, NBWIDTH, NRLEVEL, RIGINPUT, POWERONOFF, RITVALUE, 
                  RESUMEFUNC, TBURSTFUNC, TUNERFUNC, LOCKFUNC, SMETER, POWERMETER, SWRMETER, ALCMETER, COMPMETER, VOLTAGEMETER, CURRENTMETER
};

struct value {
    quint64 _value=0;
    bool _valid = false;
    bool _updated = false;
    QDateTime _dateUpdated;
};

class rigstate
{

public:

    void invalidate(stateTypes s) { map[s]._valid = false; }
    bool isValid(stateTypes s) { return map[s]._valid; }
    bool isUpdated(stateTypes s) { return map[s]._updated; }
    QDateTime whenUpdated(stateTypes s) { return map[s]._dateUpdated; }

    void set(stateTypes s, quint64 x, bool u) {
        if (x != map[s]._value) {
            _mutex.lock();
            map[s]._value = (quint64)x;
            map[s]._valid = true;
            map[s]._updated = u;
            map[s]._dateUpdated = QDateTime::currentDateTime();
            _mutex.unlock();
        }
    }
    void set(stateTypes s, qint32 x, bool u) {
        if ((quint64)x != map[s]._value) {
            _mutex.lock();
            map[s]._value = (quint64)x;
            map[s]._valid = true;
            map[s]._updated = u;
            map[s]._dateUpdated = QDateTime::currentDateTime();
            _mutex.unlock();
        }
    }
    void set(stateTypes s, quint16 x, bool u) {
        if ((quint64)x != map[s]._value) {
            _mutex.lock();
            map[s]._value = (quint64)x;
            map[s]._valid = true;
            map[s]._updated = u;
            map[s]._dateUpdated = QDateTime::currentDateTime();
            _mutex.unlock();
        }
    }
    void set(stateTypes s, quint8 x, bool u) {
        if ((quint64)x != map[s]._value) {
            _mutex.lock();
            map[s]._value = (quint64)x;
            map[s]._valid = true;
            map[s]._updated = u;
            map[s]._dateUpdated = QDateTime::currentDateTime();
            _mutex.unlock();
        }
    }
    void set(stateTypes s, bool x, bool u) {
        if ((quint64)x != map[s]._value) {
            _mutex.lock();
            map[s]._value = (quint64)x;
            map[s]._valid = true;
            map[s]._updated = u;
            map[s]._dateUpdated = QDateTime::currentDateTime();
            _mutex.unlock();
        }
    }
    void set(stateTypes s, duplexMode x, bool u) {
        if ((quint64)x != map[s]._value) {
            _mutex.lock();
            map[s]._value = (quint64)x;
            map[s]._valid = true;
            map[s]._updated = u;
            map[s]._dateUpdated = QDateTime::currentDateTime();
            _mutex.unlock();
        }
    }

    void set(stateTypes s, rigInput x, bool u) {
        if ((quint64)x != map[s]._value) {
            _mutex.lock();
            map[s]._value = (quint64)x;
            map[s]._valid = true;
            map[s]._updated = u;
            map[s]._dateUpdated = QDateTime::currentDateTime();
            _mutex.unlock();
        }
    }

    bool getBool(stateTypes s) { return map[s]._value != 0; }
    quint8 getChar(stateTypes s) { return (quint8)map[s]._value; }
    quint16 getInt16(stateTypes s) { return (qint16)map[s]._value; }
    qint32 getInt32(stateTypes s) { return (qint32)map[s]._value; }
    quint64 getInt64(stateTypes s) { return map[s]._value; }
    duplexMode getDuplex(stateTypes s) { return(duplexMode)map[s]._value; }
    rigInput getInput(stateTypes s) { return(rigInput)map[s]._value; }
    QMap<stateTypes, value> map;


private:
    //std::map<stateTypes, std::unique_ptr<valueBase> > values;
    QMutex _mutex;
};

#endif