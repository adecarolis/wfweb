#ifndef PTTYHANDLER_H
#define PTTYHANDLER_H

#include <QObject>

#include <QMutex>
#include <QDataStream>
#include <QtSerialPort/QSerialPort>

// This class abstracts the comm port in a useful way and connects to
// the command creator and command parser.

class pttyHandler : public QObject
{
    Q_OBJECT

public:
    pttyHandler();
    pttyHandler(QString portName, quint32 baudRate);
    bool serialError;

    ~pttyHandler();

private slots:
    void receiveDataIn(); // from physical port
    void receiveDataFromRigToPtty(const QByteArray& data);
    void debugThis();

signals:
    void haveTextMessage(QString message); // status, debug only
    void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
    void haveSerialPortError(const QString port, const QString error);
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
    QSerialPort* port;
    qint32 baudRate;
    unsigned char stopBits;
    bool rolledBack;

    int ptfd; // pseudo-terminal file desc.
    bool havePt;
    QString ptDevSlave;

    bool isConnected; // port opened
    mutable QMutex mutex;
    void printHex(const QByteArray& pdata, bool printVert, bool printHoriz);

};

#endif // PTTYHANDLER_H
