#include "rigcommander.h"
#include <QDebug>

#include "rigidentities.h"
#include "logcategories.h"
#include "printhex.h"

// Copyright 2017-2024 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

// This is the master class for rigCommander, subclassed by each manufacturer

rigCommander::rigCommander(QObject* parent) : QObject(parent)
{
    qInfo(logRig()) << "creating instance of rigCommander()";
    queue = cachingQueue::getInstance();

}

rigCommander::rigCommander(quint8 guid[GUIDLEN], QObject* parent) : QObject(parent)
{

    qInfo(logRig()) << "creating instance of rigCommander(guid)";
    memcpy(this->guid, guid, GUIDLEN);
    // Add some commands that is a minimum for rig detection
    queue = cachingQueue::getInstance();
}

rigCommander::~rigCommander()
{
    qInfo(logRig()) << "closing instance of rigCommander()";
}


void rigCommander::commSetup(QHash<quint8,QString> rigList, quint8 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp,quint16 tcpPort, quint8 wf)
{
    Q_UNUSED(rigList)
    Q_UNUSED(rigCivAddr)
    Q_UNUSED(rigSerialPort)
    Q_UNUSED(rigBaudRate)
    Q_UNUSED(vsp)
    Q_UNUSED(tcpPort)
    Q_UNUSED(wf)
    qWarning(logRig()) << "commSetup() (serial) not implemented by rig type";
}

void rigCommander::commSetup(QHash<quint8,QString> rigList, quint8 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcpPort)
{
    Q_UNUSED(rigList)
    Q_UNUSED(rigCivAddr)
    Q_UNUSED(prefs)
    Q_UNUSED(rxSetup)
    Q_UNUSED(txSetup)
    Q_UNUSED(vsp)
    Q_UNUSED(tcpPort)
    qWarning(logRig()) << "commSetup() (network) not implemented by rig type";
}

void rigCommander::closeComm()
{
    qWarning(logRig()) << "closeComm() not implemented by rig type";
}

void rigCommander::process()
{
    qWarning(logRig()) << "process() not implemented by rig type";
}

void rigCommander::getRigID()
{
    qWarning(logRig()) << "getRigID() not implemented by rig type";
}

void rigCommander::setRigID(quint8 rigID)
{
    Q_UNUSED(rigID)

    qWarning(logRig()) << "setRigID() not implemented by rig type";
}

void rigCommander::receiveBaudRate(quint32 baudrate)
{
    Q_UNUSED(baudrate)

    qWarning(logRig()) << "receiveBaudRate() not implemented by rig type";
}

void rigCommander::setRTSforPTT(bool enabled)
{
    Q_UNUSED(enabled)

    qWarning(logRig()) << "setRTSforPTT() not implemented by rig type";
}

void rigCommander::findRigs()
{
    qWarning(logRig()) << "findRigs() not implemented by rig type";
}


void rigCommander::powerOn()
{
    qWarning(logRig()) << "powerOn() not implemented by rig type";
}

void rigCommander::powerOff()
{
    qWarning(logRig()) << "powerOff() not implemented by rig type";
}

void rigCommander::setCIVAddr(quint8 civAddr)
{
    Q_UNUSED(civAddr)

    qWarning(logRig()) << "setCIVAddr() not implemented by rig type";
}

void rigCommander::receiveCommand(funcs func, QVariant value, uchar receiver)
{
    Q_UNUSED(func)
    Q_UNUSED(value)
    Q_UNUSED(receiver)

    qWarning(logRig()) << "receiveCommand() not implemented by rig type";
}

void rigCommander::setAfGain(quint8 level)
{
    Q_UNUSED(level)

    qWarning(logRig()) << "setAfGain() not implemented by rig type";
}

/*
 * The following slots/functions are only implemented
 * in the parent rigCommander()
 *
*/

void rigCommander::handlePortError(errorType err)
{
    qInfo(logRig()) << "Error using port " << err.device << " message: " << err.message;
    emit havePortError(err);
}

void rigCommander::handleStatusUpdate(const networkStatus status)
{
    emit haveStatusUpdate(status);
}

void rigCommander::handleNetworkAudioLevels(networkAudioLevels l)
{
    emit haveNetworkAudioLevels(l);
}

void rigCommander::handleNewData(const QByteArray& data)
{
    emit haveDataForServer(data);
}

void rigCommander::receiveAudioData(const audioPacket& data)
{
    emit haveAudioData(data);
}


void rigCommander::changeLatency(const quint16 value)
{
    emit haveChangeLatency(value);
}

void rigCommander::radioSelection(QList<radio_cap_packet> radios)
{
    emit requestRadioSelection(radios);
}

void rigCommander::radioUsage(quint8 radio, bool admin, quint8 busy, QString user, QString ip) {
    emit setRadioUsage(radio, admin, busy, user, ip);
}

void rigCommander::setCurrentRadio(quint8 radio) {
    emit selectedRadio(radio);
}

void rigCommander::getDebug()
{
    // generic debug function for development.
    emit getMoreDebug();
}

void rigCommander::dataFromServer(QByteArray data)
{
    emit dataForComm(data);
}

bool rigCommander::usingLAN()
{
    return usingNativeLAN;
}

quint8* rigCommander::getGUID() {
    return guid;
}

void rigCommander::printHex(const QByteArray &pdata)
{
    printHex(pdata, false, true);
}

void rigCommander::printHex(const QByteArray &pdata, bool printVert, bool printHoriz)
{
    qDebug(logRig()) << "---- Begin hex dump -----:";
    QString sdata("DATA:  ");
    QString index("INDEX: ");
    QStringList strings;

    for(int i=0; i < pdata.length(); i++)
    {
        strings << QString("[%1]: %2").arg(i,8,10,QChar('0')).arg((quint8)pdata[i], 2, 16, QChar('0'));
        sdata.append(QString("%1 ").arg((quint8)pdata[i], 2, 16, QChar('0')) );
        index.append(QString("%1 ").arg(i, 2, 10, QChar('0')));
    }

    if(printVert)
    {
        for(int i=0; i < strings.length(); i++)
        {
            //sdata = QString(strings.at(i));
            qDebug(logRig()) << strings.at(i);
        }
    }

    if(printHoriz)
    {
        qDebug(logRig()) << index;
        qDebug(logRig()) << sdata;
    }
    qDebug(logRig()) << "----- End hex dump -----";
}
