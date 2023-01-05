#ifndef PTTYHANDLER_H
#define PTTYHANDLER_H

#include <QObject>

#include <QMutex>
#include <QDataStream>
#include <QIODevice>
#include <QSocketNotifier>
#include <QtSerialPort/QSerialPort>

#include "rigidentities.h"
#include "wfviewtypes.h"

// This class abstracts the comm port in a useful way and connects to
// the command creator and command parser.

class pttyHandler : public QObject
{
    Q_OBJECT

public:
    explicit pttyHandler(QString portName, QObject* parent = nullptr);
    pttyHandler(QString portName, quint32 baudRate);
    bool serialError;

    ~pttyHandler();

private slots:
    void receiveDataIn(int fd); // from physical port
    void receiveDataFromRigToPtty(const QByteArray& data);
    void debugThis();
    void receiveFoundRigID(rigCapabilities rigCaps);

signals:
    void haveTextMessage(QString message); // status, debug only
    void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
    void havePortError(errorType err);
    void haveStatusUpdate(const QString text);

private:
    void setupPtty();
    void openPort();
    void closePort();

    void sendDataOut(const QByteArray& writeData); // out to radio
    void debugMe();
    void hexPrint();

    //QDataStream stream;
    QByteArray outPortData;
    QByteArray inPortData;

    //QDataStream outStream;
    //QDataStream inStream;

    unsigned char buffer[256];

    QString portName;
    QSerialPort* port = Q_NULLPTR;
    qint32 baudRate;
    unsigned char stopBits;
    bool rolledBack;

    int ptfd; // pseudo-terminal file desc.
    int ptKeepAlive=0; // Used to keep the pty alive after client disconnects.
    bool havePt;
    QString ptDevSlave;

    bool isConnected=false; // port opened
    mutable QMutex mutex;
    void printHex(const QByteArray& pdata, bool printVert, bool printHoriz);
    bool disableTransceive = false;
    QSocketNotifier *ptReader = Q_NULLPTR;
    quint8 civId=0;
    rigCapabilities rigCaps;
};

#endif // PTTYHANDLER_H
