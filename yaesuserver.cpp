// Copyright 2017-2025 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

#include "yaesuserver.h"
#include "logcategories.h"

yaesuServer::yaesuServer(SERVERCONFIG* config, rigServer* parent) :
    rigServer(config,parent)
{
    qInfo(logRigServer()) << "Creating instance of Yaesu server";
}

void yaesuServer::init()
{
    connect(queue,SIGNAL(rigCapsUpdated(rigCapabilities*)),this, SLOT(receiveRigCaps(rigCapabilities*)));
}

yaesuServer::~yaesuServer()
{
}

void yaesuServer::dataForServer(QByteArray d)
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    //qInfo(logRigServer()) << "Received data for server clients";
    if (sender == Q_NULLPTR)
    {
        return;
    }


    return;
}


void yaesuServer::receiveAudioData(const audioPacket& d)
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());
    quint8 guid[GUIDLEN];
    if (sender != Q_NULLPTR)
    {
        memcpy(guid, sender->getGUID(), GUIDLEN);
    }
    else {
        memcpy(guid, d.guid, GUIDLEN);
    }

    return;
}
