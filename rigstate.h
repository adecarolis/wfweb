#ifndef RIGSTATE_H
#define RIGSTATE_H

#include <QObject>
#include <QMutex>

#include "rigcommander.h"

// Meters at the end as they are ALWAYS updated from the rig!
enum stateTypes { NONE, VFOAFREQ, VFOBFREQ, CURRENTVFO, PTT, MODE, FILTER, DUPLEX, DATAMODE, ANTENNA, RXANTENNA, CTCSS, TSQL, DTCS, CSQL,
                  PREAMP, AGC, ATTENUATOR, MODINPUT, AFGAIN, RFGAIN, SQUELCH, TXPOWER, MICGAIN, COMPLEVEL, MONITORLEVEL, VOXGAIN, ANTIVOXGAIN,
                  FAGCFUNC, NBFUNC, COMPFUNC, VOXFUNC, TONEFUNC, TSQLFUNC, SBKINFUNC, FBKINFUNC, ANFFUNC, NRFUNC, AIPFUNC, APFFUNC, MONFUNC, MNFUNC,RFFUNC,
                  AROFUNC, MUTEFUNC, VSCFUNC, REVFUNC, SQLFUNC, ABMFUNC, BCFUNC, MBCFUNC, RITFUNC, AFCFUNC, SATMODEFUNC, SCOPEFUNC,
                  RESUMEFUNC, TBURSTFUNC, TUNERFUNC, LOCKFUNC, SMETER, POWERMETER, SWRMETER, ALCMETER, COMPMETER, VOLTAGEMETER, CURRENTMETER };


class rigstate
{

public:

    void lock() { _mutex.lock(); return; }
    void unlock() { _mutex.unlock(); return; }

    quint64 updated() { return _updated; }
    void updated(int update) { _mutex.lock(); _updated |= 1ULL << update; _mutex.unlock(); }

    quint64 vfoAFreq() { return _vfoAFreq; }
    void vfoAFreq(quint64 vfoAFreq, int update) { _mutex.lock();  _vfoAFreq = vfoAFreq; _updated |= 1ULL << update; _mutex.unlock(); }

    quint64 vfoBFreq() { return _vfoBFreq; }
    void vfoBFreq(quint64 vfoBFreq, int update) { _mutex.lock();  _vfoBFreq = vfoBFreq; _updated |= 1ULL << update; _mutex.unlock(); }

    unsigned char currentVfo() { return _currentVfo; }
    void currentVfo(unsigned char currentVfo, int update) { _mutex.lock(); _currentVfo = currentVfo; _updated |= 1ULL << update; _mutex.unlock(); }

    bool ptt() { return _ptt; }
    void ptt(bool ptt, int update) { _mutex.lock(); _ptt = ptt; _updated |= 1ULL << update; _mutex.unlock(); }

    unsigned char mode() { return _mode; }
    void mode(unsigned char mode, int update) { _mutex.lock(); _mode = mode; _updated |= 1ULL << update; _mutex.unlock(); }

    unsigned char filter() { return _filter; }
    void filter(unsigned char filter,  int update) { _mutex.lock(); _filter = filter; _updated |= 1ULL << update; _mutex.unlock();}

    duplexMode duplex() { return _duplex; }
    void duplex(duplexMode duplex,  int update) { _mutex.lock(); _duplex = duplex; _updated |= 1ULL << update; _mutex.unlock();}

    bool datamode() { return _datamode; }
    void datamode(bool datamode,  int update) { _mutex.lock(); _datamode = datamode; _updated |= 1ULL << update; _mutex.unlock();}

    unsigned char antenna() { return _antenna; }
    void antenna(unsigned char antenna,  int update) { _mutex.lock(); _antenna = antenna; _updated |= 1ULL << update; _mutex.unlock();}

    bool rxAntenna() { return _rxAntenna; }
    void rxAntenna(bool rxAntenna, int update) { _mutex.lock(); _rxAntenna = rxAntenna; _updated |= 1ULL << update; _mutex.unlock(); }
    quint16 ctcss() { return _ctcss; }
    void ctcss(quint16 ctcss,  int update) { _mutex.lock(); _ctcss = ctcss; _updated |= 1ULL << update; _mutex.unlock(); }
    quint16 tsql() { return _tsql; }
    void tsql(quint16 tsql,  int update) { _mutex.lock(); _tsql = tsql; _updated |= 1ULL << update; _mutex.unlock(); }
    quint16 dtcs() { return _dtcs; }
    void dtcs(quint16 dtcs,  int update) { _mutex.lock(); _dtcs = dtcs; _updated |= 1ULL << update; _mutex.unlock(); }
    quint16 csql() { return _csql; }
    void csql(quint16 csql,  int update) { _mutex.lock(); _csql = csql; _updated |= 1ULL << update; _mutex.unlock(); }

    unsigned char preamp() { return _preamp; }
    void preamp(unsigned char preamp, int update) { _mutex.lock(); _preamp = preamp; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char agc() { return _agc; }
    void agc(unsigned char agc, int update) { _mutex.lock(); _agc = agc; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char attenuator() { return _attenuator; }
    void attenuator(unsigned char attenuator, int update) { _mutex.lock(); _attenuator = attenuator; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char modInput() { return _modInput; }
    void modInput(unsigned char modInput, int update) { _mutex.lock(); _modInput = modInput; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char afGain() { return _afGain; }
    void afGain(unsigned char afGain, int update) { _mutex.lock(); _afGain = afGain; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char rfGain() { return _rfGain; }
    void rfGain(unsigned char rfGain, int update) { _mutex.lock(); _rfGain = rfGain; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char squelch() { return _squelch; }
    void squelch(unsigned char squelch, int update) { _mutex.lock(); _squelch = squelch; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char txPower() { return _txPower; }
    void txPower(unsigned char txPower, int update) { _mutex.lock(); _txPower = txPower; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char micGain() { return _micGain; }
    void micGain(unsigned char micGain, int update) { _mutex.lock(); _micGain = micGain; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char compLevel() { return _compLevel; }
    void compLevel(unsigned char compLevel, int update) { _mutex.lock(); _compLevel = compLevel; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char monitorLevel() { return _monitorLevel; }
    void monitorLevel(unsigned char monitorLevel, int update) { _mutex.lock(); _monitorLevel = monitorLevel; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char voxGain() { return _voxGain; }
    void voxGain(unsigned char voxGain, int update) { _mutex.lock(); _voxGain = voxGain; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char antiVoxGain() { return _antiVoxGain; }
    void antiVoxGain(unsigned char antiVoxGain, int update) { _mutex.lock(); _antiVoxGain = antiVoxGain; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char nbLevel() { return _nbLevel; }
    void nbLevel(unsigned char nbLevel, int update) { _mutex.lock(); _nbLevel = nbLevel; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char nbDepth() { return _nbDepth; }
    void nbDepth(unsigned char nbDepth, int update) { _mutex.lock(); _nbDepth = nbDepth; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char nbWidth() { return _nbWidth; }
    void nbWidth(unsigned char nbWidth, int update) { _mutex.lock(); _nbWidth = nbWidth; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char nrLevel() { return _nrLevel; }
    void nrLevel(unsigned char nrLevel, int update) { _mutex.lock(); _nrLevel = nrLevel; _updated |= 1ULL << update; _mutex.unlock(); }

    unsigned char sMeter() { return _sMeter; }
    void sMeter(unsigned char sMeter, int update) { _mutex.lock(); _sMeter = sMeter; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char powerMeter() { return _powerMeter; }
    void powerMeter(unsigned char powerMeter, int update) { _mutex.lock(); _powerMeter = powerMeter; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char swrMeter() { return _swrMeter; }
    void swrMeter(unsigned char swrMeter, int update) { _mutex.lock(); _swrMeter = swrMeter; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char alcMeter() { return _alcMeter; }
    void alcMeter(unsigned char alcMeter, int update) { _mutex.lock(); _alcMeter = alcMeter; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char compMeter() { return _compMeter; }
    void compMeter(unsigned char compMeter, int update) { _mutex.lock(); _compMeter = compMeter; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char voltageMeter() { return _voltageMeter; }
    void voltageMeter(unsigned char voltageMeter, int update) { _mutex.lock(); _voltageMeter = voltageMeter; _updated |= 1ULL << update; _mutex.unlock(); }
    unsigned char currentMeter() { return _currentMeter; }
    void currentMeter(unsigned char currentMeter, int update) { _mutex.lock(); _currentMeter = currentMeter; _updated |= 1ULL << update; _mutex.unlock(); }

    /*FAGCFUNC, NBFUNC, COMPFUNC, VOXFUNC, TONEFUNC, TSQLFUNC, SBKINFUNC, FBKINFUNC, ANFFUNC, NRFUNC, AIPFUNC, APFFUNC, MONFUNC, MNFUNC,RFFUNC,
    */
    bool fagcFunc() { return _fagcFunc; }
    void fagcFunc(bool fagcFunc, int update) { _mutex.lock(); _fagcFunc = fagcFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool nbFunc() { return _nbFunc; }
    void nbFunc(bool nbFunc, int update) { _mutex.lock(); _nbFunc = nbFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool compFunc() { return _compFunc; }
    void compFunc(bool compFunc, int update) { _mutex.lock(); _compFunc = compFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool voxFunc() { return _voxFunc; }
    void voxFunc(bool voxFunc, int update) { _mutex.lock(); _voxFunc = voxFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool toneFunc() { return _toneFunc; }
    void toneFunc(bool toneFunc, int update) { _mutex.lock(); _toneFunc = toneFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool tsqlFunc() { return _tsqlFunc; }
    void tsqlFunc(bool tsqlFunc, int update) { _mutex.lock(); _tsqlFunc = tsqlFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool sbkinFunc() { return _sbkinFunc; }
    void sbkinFunc(bool sbkinFunc, int update) { _mutex.lock(); _sbkinFunc = sbkinFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool fbkinFunc() { return _fbkinFunc; }
    void fbkinFunc(bool fbkinFunc, int update) { _mutex.lock(); _fbkinFunc = fbkinFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool anfFunc() { return _anfFunc; }
    void anfFunc(bool anfFunc, int update) { _mutex.lock(); _anfFunc = anfFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool nrFunc() { return _nrFunc; }
    void nrFunc(bool nrFunc, int update) { _mutex.lock(); _nrFunc = nrFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool aipFunc() { return _aipFunc; }
    void aipFunc(bool aipFunc, int update) { _mutex.lock(); _aipFunc = aipFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool apfFunc() { return _apfFunc; }
    void apfFunc(bool apfFunc, int update) { _mutex.lock(); _apfFunc = apfFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool monFunc() { return _monFunc; }
    void monFunc(bool monFunc, int update) { _mutex.lock(); _monFunc = monFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool mnFunc() { return _mnFunc; }
    void mnFunc(bool mnFunc, int update) { _mutex.lock(); _mnFunc = mnFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool rfFunc() { return _rfFunc; }
    void rfFunc(bool rfFunc, int update) { _mutex.lock(); _rfFunc = rfFunc; _updated |= 1ULL << update; _mutex.unlock(); }

    /* AROFUNC, MUTEFUNC, VSCFUNC, REVFUNC, SQLFUNC, ABMFUNC, BCFUNC, MBCFUNC, RITFUNC, AFCFUNC, SATMODEFUNC, SCOPEFUNC,
        RESUMEFUNC, TBURSTFUNC, TUNERFUNC}; */
    bool aroFunc() { return _aroFunc; }
    void aroFunc(bool aroFunc, int update) { _mutex.lock(); _aroFunc = aroFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool muteFunc() { return _muteFunc; }
    void muteFunc(bool muteFunc, int update) { _mutex.lock(); _muteFunc = muteFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool vscFunc() { return _vscFunc; }
    void vscFunc(bool vscFunc, int update) { _mutex.lock(); _vscFunc = vscFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool revFunc() { return _revFunc; }
    void revFunc(bool revFunc, int update) { _mutex.lock(); _revFunc = revFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool sqlFunc() { return _sqlFunc; }
    void sqlFunc(bool sqlFunc, int update) { _mutex.lock(); _sqlFunc = sqlFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool abmFunc() { return _abmFunc; }
    void abmFunc(bool abmFunc, int update) { _mutex.lock(); _abmFunc = abmFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool bcFunc() { return _bcFunc; }
    void bcFunc(bool bcFunc, int update) { _mutex.lock(); _bcFunc = bcFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool mbcFunc() { return _mbcFunc; }
    void mbcFunc(bool mbcFunc, int update) { _mutex.lock(); _mbcFunc = mbcFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool ritFunc() { return _ritFunc; }
    void ritFunc(bool ritFunc, int update) { _mutex.lock(); _ritFunc = ritFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool afcFunc() { return _afcFunc; }
    void afcFunc(bool afcFunc, int update) { _mutex.lock(); _afcFunc = afcFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool satmodeFunc() { return _satmodeFunc; }
    void satmodeFunc(bool satmodeFunc, int update) { _mutex.lock(); _satmodeFunc = satmodeFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool scopeFunc() { return _scopeFunc; }
    void scopeFunc(bool scopeFunc, int update) { _mutex.lock(); _scopeFunc = scopeFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool resumeFunc() { return _resumeFunc; }
    void resumeFunc(bool resumeFunc, int update) { _mutex.lock(); _resumeFunc = resumeFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool tburstFunc() { return _tburstFunc; }
    void tburstFunc(bool tburstFunc, int update) { _mutex.lock(); _tburstFunc = tburstFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool tunerFunc() { return _tunerFunc; }
    void tunerFunc(bool tunerFunc, int update) { _mutex.lock(); _tunerFunc = tunerFunc; _updated |= 1ULL << update; _mutex.unlock(); }
    bool lockFunc() { return _lockFunc; }
    void lockFunc(bool lockFunc, int update) { _mutex.lock(); _lockFunc = lockFunc; _updated |= 1ULL << update; _mutex.unlock(); }


private:
    QMutex _mutex;
    quint64 _updated=0;
    quint64 _vfoAFreq=0;
    quint64 _vfoBFreq=0;
    unsigned char _currentVfo=0;
    bool _ptt=0;
    unsigned char _mode=0;
    unsigned char _filter=0;
    duplexMode _duplex;
    bool _datamode=0;
    unsigned char _antenna=0;
    bool _rxAntenna=0;
    // Tones
    quint16 _ctcss;
    quint16 _tsql;
    quint16 _dtcs;
    quint16 _csql;
    // Levels
    unsigned char _preamp;
    unsigned char _agc;
    unsigned char _attenuator;
    unsigned char _modInput;
    unsigned char _afGain;
    unsigned char _rfGain;
    unsigned char _squelch;
    unsigned char _txPower;
    unsigned char _micGain;
    unsigned char _compLevel;
    unsigned char _monitorLevel;
    unsigned char _voxGain;
    unsigned char _antiVoxGain;
    unsigned char _nrLevel;
    unsigned char _nbLevel;
    unsigned char _nbDepth;
    unsigned char _nbWidth;
    // Meters
    unsigned char _sMeter;
    unsigned char _powerMeter;
    unsigned char _swrMeter;
    unsigned char _alcMeter;
    unsigned char _compMeter;
    unsigned char _voltageMeter;
    unsigned char _currentMeter;
    // Functions
    bool _fagcFunc = false;
    bool _nbFunc = false;
    bool _compFunc = false;
    bool _voxFunc = false;
    bool _toneFunc = false;
    bool _tsqlFunc = false;
    bool _sbkinFunc = false;
    bool _fbkinFunc = false;
    bool _anfFunc = false;
    bool _nrFunc = false;
    bool _aipFunc = false;
    bool _apfFunc = false;
    bool _monFunc = false;
    bool _mnFunc = false;
    bool _rfFunc = false;
    bool _aroFunc = false;
    bool _muteFunc = false;
    bool _vscFunc = false;
    bool _revFunc = false;
    bool _sqlFunc = false;
    bool _abmFunc = false;
    bool _bcFunc = false;
    bool _mbcFunc = false;
    bool _ritFunc = false;
    bool _afcFunc = false;
    bool _satmodeFunc = false;
    bool _scopeFunc = false;
    bool _resumeFunc = false;
    bool _tburstFunc = false;
    bool _tunerFunc = false;
    bool _lockFunc = false;

};

#endif