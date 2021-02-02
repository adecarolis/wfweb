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
    commHandler(QString portName, quint32 baudRate);
    bool serialError;

    ~commHandler();

private slots:
    void receiveDataIn(); // from physical port
    void receiveDataInPt(); // from pseudo-term
    void receiveDataFromUserToRig(const QByteArray &data);
    void debugThis();

signals:
    void haveTextMessage(QString message); // status, debug only
    void sendDataOutToPort(const QByteArray &writeData); // not used
    void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
    void haveSerialPortError(const QString port, const QString error);

private:
    void setupComm();
    void openPort();
    void closePort();

    void initializePt(); // like ch constructor
    void setupPtComm();
    void openPtPort();

    void sendDataOut(const QByteArray &writeData); // out to radio
    void sendDataOutPt(const QByteArray &writeData); // out to pseudo-terminal
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

    QSerialPort *pseudoterm;
    int ptfd; // pseudo-terminal file desc.
    mutable QMutex ptMutex;
    bool havePt;
    QString ptDevSlave;

    bool isConnected; // port opened
    mutable QMutex mutex;
    void printHex(const QByteArray &pdata, bool printVert, bool printHoriz);

};

#endif // COMMHANDLER_H
