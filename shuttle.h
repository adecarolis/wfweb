#ifndef SHUTTLE_H
#define SHUTTLE_H

#include <QObject>
#include <QtUsb/QUsb>
#include <QtUsb/QHidDevice>

const quint8 USB_ENDPOINT_IN = 0x81; /* Bulk output endpoint for responses */
const quint8 USB_ENDPOINT_OUT = 0x01; /* Bulk input endpoint for commands */
const quint16 USB_TIMEOUT_MSEC = 300;

class shuttle : public QObject
{
    Q_OBJECT
public:
    explicit shuttle(QObject* parent = Q_NULLPTR);
    ~shuttle(void);
    bool openDevice(void);
    void closeDevice(void);
    void read(QByteArray* buf);
    void write(QByteArray* buf);

signals:

private:
    QUsb m_usb;
    QHidDevice* m_hid_dev;
    QByteArray m_send, m_recv;
};


#endif