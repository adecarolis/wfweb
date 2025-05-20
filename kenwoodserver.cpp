// Copyright 2017-2025 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

#include "kenwoodserver.h"
#include "logcategories.h"

kenwoodServer::kenwoodServer(SERVERCONFIG* config, rigServer* parent) :
    rigServer(config,parent)
{
    qInfo(logRigServer()) << "Creating instance of Kenwood server";
}

void kenwoodServer::init()
{
    connect(queue,SIGNAL(rigCapsUpdated(rigCapabilities*)),this, SLOT(receiveRigCaps(rigCapabilities*)));

    server = new QTcpServer(this);

    if (!server->listen(QHostAddress::Any, config->controlPort)) {
        qInfo(logRigServer()) << "could not start on port " << config->controlPort;
        return;
    }
    else
    {
        qInfo(logRigServer()) << "started on port " << config->controlPort;
    }
    connect(server, &QTcpServer::newConnection, this, &kenwoodServer::newConnection);
}

kenwoodServer::~kenwoodServer()
{
    // We should probably step through all connected clients (just in case)

    /*
    for (auto it = clients.begin(); it != clients.end(); it++)
    {
        it.key()->close();
        delete it.key();
        delete it.value();
    }

    if (server != Q_NULLPTR)
    {
        server->close();
    }

    */

}

void kenwoodServer::dataForServer(QByteArray d)
{
    kenwoodCommander* sender = qobject_cast<kenwoodCommander*>(QObject::sender());

    if (sender == Q_NULLPTR)
    {
        qInfo(logRigServer()) << "Object type didn't match!";
        //return;
    }

    //qInfo(logRigServer()) << "Radio reply:" << d;

    for (auto it = clients.begin(); it != clients.end(); it++)
    {
        QTcpSocket* socket = it.key();
        CLIENT* client = it.value();
        if (socket != Q_NULLPTR && client != Q_NULLPTR && socket->isOpen() && client->connected && client->authenticated)
        {
            socket->write(d);
        }
    }

    return;
}


void kenwoodServer::receiveAudioData(const audioPacket& d)
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

void kenwoodServer::newConnection()
{
    // A new client has connected!
    CLIENT* current = new CLIENT();
    QTcpSocket* socket = server->nextPendingConnection();
    connect(socket, &QTcpSocket::disconnected, socket, [this,socket] { disconnected(socket);});
    connect(socket, &QTcpSocket::readyRead, socket, [this,socket] { readyRead(socket);});

    current->ipAddress = socket->peerAddress();

    clients.insert(socket,current);
    qInfo(logRigServer()) << "Client connected!" << socket->peerAddress() << ":" << socket->peerPort();
}

void kenwoodServer::disconnected(QTcpSocket* socket)
{
    // Client has disconnected!
    qInfo(logRigServer()) << "Client disconnected!" << socket->peerAddress() << ":" << socket->peerPort();
    if (socket->isOpen())
        socket->close();
    clients.remove(socket);
}

void kenwoodServer::readyRead(QTcpSocket* socket)
{
    // Data from client
    auto it = clients.find(socket);
    if (it == clients.end())
    {
        qWarning(logRigServer()) << "Client not found, this shouldn't happen!";
        disconnected(socket);
        return;
    }

    CLIENT *c = it.value();
    c->buffer.append(socket->readAll());

    if (c->buffer.endsWith(";") || c->buffer.endsWith((";\n")))
    {
        QList<QByteArray> commands = c->buffer.split(';');
        for (auto &d: commands) {
            if (d.isEmpty() || d == QString("\n").toLatin1())
                continue;
            // We have at least one full command, so process it
            if (!c->connected)
            {
                if (d == ("##CN")) {
                    socket->write(QString("##CN1;").toLatin1());
                    c->connected = true;
                    continue;
                }
                else
                {
                    disconnected(socket);
                    return;
                }
            } else if (!c->authenticated) {
                if (d.startsWith("##ID")) {
                    // This is an authentication request
                    uchar type = d.mid(4,1).toShort();
                    uchar userlen = d.mid(5,2).toUShort();
                    uchar passlen = d.mid(7,2).toUShort();
                    QString username = d.mid(9,userlen);
                    QString password = d.mid(9+userlen,passlen);
                    QByteArray passcomp;
                    passcode(password, passcomp);
                    qInfo(logRigServer()) << c->ipAddress.toString() << ": Received 'login'";
                    foreach(SERVERUSER user, config->users)
                    {
                        if (user.username == username && user.password == passcomp)
                        {
                            c->authenticated = true;
                            c->user = username;
                            // Usertypes 0=admin, 1=user, 2=rx only.
                            if (user.userType == 0 && type == 0)
                            {
                                c->admin = true;
                                c->tx = true;
                            } else if (user.userType == 1 && type == 1)
                            {
                                c->admin=false;
                                c->tx=true;
                            }
                            else if (user.userType == 2 && type == 1)
                            {
                                c->admin=false;
                                c->tx=false;
                            } else
                            {
                                qInfo(logRigServer()) << "User type did not match stored type";
                                c->authenticated = false;
                                disconnected(socket);
                                return;
                            }
                            qInfo(logRigServer()) << "User" << username << "logged-in successfully";
                            socket->write(QString("##ID1;##UE1;##TI%0;").arg(uchar(c->tx)).toLatin1());
                            break;
                        }
                    }
                    if (c->authenticated != true)
                    {
                        qInfo(logRigServer()) << "User" << username << "invalid password";
                        socket->write(QString("##ID0").toLatin1());
                        disconnected(socket);
                        return;
                    } else {
                        continue;
                    }
                } else {
                    qInfo(logRigServer()) << "Invalid command received" << d;
                    disconnected(socket);
                    return;
                }
            }
            // If we got here then user is both connected and authenticated.
            if (d.startsWith("##VP")) {
                //User is requesting voip (audio) control
                uchar vp = uchar(d.mid(4,1).toUShort());
                if (!c->isStreaming && vp==1)
                {
                    qInfo(logRigServer()) << "Starting high quality audio streaming";
                }
                else if (!c->isStreaming && vp==2)
                {
                    qInfo(logRigServer()) << "Starting low quality audio streaming";
                }
                else
                {
                    qInfo(logRigServer()) << "Stopping audio streaming";
                }
            } else {
                // I think all other commands need to be forwarded to the radio?
                // We have removed the semicolon, so add it back.
                d.append(QString(";").toLatin1());
                //qInfo(logRigServer()) << "Sending:" << d;
                emit haveDataFromServer(d);
            }
        }
        // We have processed this buffer, so lets get rid of it.
        c->buffer.clear();
    }


}
