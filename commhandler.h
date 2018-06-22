#ifndef COMMHANDLER_H
#define COMMHANDLER_H

#include <QObject>

#include <QMutex>
#include <QDataStream>
#include <QtSerialPort/QSerialPort>

// This class abstracts the comm port in a useful way and connects to
// the command creator and command parser.

class commHandler : public QObject
{
    Q_OBJECT

public:
    commHandler();
    ~commHandler();

private slots:
    void receiveDataIn();
    void receiveDataFromUserToRig(const QByteArray &data);
    void debugThis();

signals:
    void haveTextMessage(QString message); // status, debug
    void sendDataOutToPort(const QByteArray &writeData);
    void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander

private:
    void setupComm();
    void openPort();
    void closePort();

    void sendDataOut(const QByteArray &writeData);
    void debugMe();
    void hexPrint();

    //QDataStream stream;
    QByteArray outPortData;
    QByteArray inPortData;

    //QDataStream outStream;
    //QDataStream inStream;

    unsigned char buffer[256];

    QString portName;
    QSerialPort *port;
    qint32 baudrate;
    unsigned char stopbits;
    bool rolledBack;


    bool isConnected; // port opened
    mutable QMutex mutex;
    void printHex(const QByteArray &pdata, bool printVert, bool printHoriz);

};

#endif // COMMHANDLER_H
