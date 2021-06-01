#ifndef SHUTTLE_H
#define SHUTTLE_H

#include <QObject>
#include <QtUsb/QUsb>
#include <QtUsb/QHidDevice>
#include <QThread>



class shuttle : public QObject
{
    Q_OBJECT
public:
    explicit shuttle(int vid, int pid);
    ~shuttle(void);
    bool openDevice(void);
    void closeDevice(void);
    void read(QByteArray* buf);
    void write(QByteArray* buf);

public slots:
    void process();
    void init();

signals:
    void finished();
    void error(QString err);

private:
    QUsb m_usb;
    QHidDevice* m_hid_dev = Q_NULLPTR;
    QByteArray m_send, m_recv;
    int vid;
    int pid;
};


#endif