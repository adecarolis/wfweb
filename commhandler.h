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
    bool rtsStatus();

    ~commHandler();

public slots:
    void setUseRTSforPTT(bool useRTS);
    void setRTS(bool rtsOn);

private slots:
    void receiveDataIn(); // from physical port
    void receiveDataFromUserToRig(const QByteArray &data);
    void debugThis();

signals:
    void haveTextMessage(QString message); // status, debug only
    void sendDataOutToPort(const QByteArray &writeData); // not used
    void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
    void haveSerialPortError(const QString port, const QString error);
    void haveStatusUpdate(const QString text);

private:
    void setupComm();
    void openPort();
    void closePort();


    void sendDataOut(const QByteArray &writeData); // out to radio
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

    bool PTTviaRTS = false;

    bool isConnected; // port opened
    mutable QMutex mutex;
    void printHex(const QByteArray &pdata, bool printVert, bool printHoriz);

};

#endif // COMMHANDLER_H
