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
    Q_UNUSED(caps)
    qWarning(logRigServer()) << "receiveRigCaps() not implemented by server type";
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
