// Copyright 2017-2025 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

#include "rigserver.h"
#include "logcategories.h"

rigServer::rigServer(SERVERCONFIG* config, QObject* parent) :
    QObject(parent), config(config)
{
    qInfo(logRigServer()) << "Creating instance of rigServer";
    queue=cachingQueue::getInstance();
}

rigServer::~rigServer()
{
    qInfo(logRigServer()) << "Closing instance of rigServer";
}

void rigServer::init()
{
    qWarning(logRigServer()) << "init() not implemented by server type";
}

void rigServer::receiveRigCaps(rigCapabilities* caps)
{
    if (caps == Q_NULLPTR)
        return;

    foreach (RIGCONFIG* rig, config->rigs) {
        if (!memcmp(rig->guid, caps->guid, GUIDLEN) || config->rigs.size()==1) {
            // Matching rig, fill-in missing details
            qInfo(logRigServer()) << "Received new rigCaps for:" << caps->modelName << "CIV:" << QString::number(caps->modelID,16);
            rig->rigAvailable = true;
            rig->modelName = caps->modelName;
            rig->civAddr = caps->modelID;
            if (rig->rigName == "<NONE>") {
                rig->rigName = caps->modelName;
            }

            qInfo(logRigServer()) << "Model Number:" << rig->civAddr;
            qInfo(logRigServer()) << "Model:" << rig->modelName;
            qInfo(logRigServer()) << "Name:" << rig->rigName;
            qInfo(logRigServer()).noquote() << QString("GUID: {%1%2%3%4-%5%6-%7%8-%9%10-%11%12%13%14%15%16}")
               .arg(rig->guid[0], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[1], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[2], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[3], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[4], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[5], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[6], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[7], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[8], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[9], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[10], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[11], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[12], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[13], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[14], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[15], 2, 16, QLatin1Char('0'));
        }
    }
}

void rigServer::dataForServer(QByteArray d)
{
    Q_UNUSED(d)
    qWarning(logRigServer()) << "dataForServer() not implemented by server type";
    return;
}


void rigServer::receiveAudioData(const audioPacket& d)
{
    Q_UNUSED(d)
    qWarning(logRigServer()) << "receiveAudioData() not implemented by server type";
    return;
}
