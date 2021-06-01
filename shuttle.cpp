#include "shuttle.h"
#include <QDebug>

shuttle::shuttle(int vid, int pid):
    vid(vid),
    pid(pid)
{

}

shuttle::~shuttle()
{
    this->closeDevice();
    m_hid_dev->deleteLater();
}


void shuttle::init()
{
    m_hid_dev = new QHidDevice();

    qDebug("Opening shuttle");

    m_hid_dev->open(vid, pid);

    qDebug() << "Manufacturer:" << m_hid_dev->manufacturer();
    qDebug() << "Product:" << m_hid_dev->product();

    return;
}

void shuttle::closeDevice()
{
    qDebug("Closing");

    if (m_hid_dev->isOpen()) {
        m_hid_dev->close();
    }
}

void shuttle::read(QByteArray* buf)
{
    QByteArray b(128, '0');
    m_hid_dev->read(&b, b.size());

    qDebug() << "Reading" << b << b.size();
    buf->append(b);
}

void shuttle::write(QByteArray* buf)
{
    qDebug() << "Writing" << *buf << buf->size();
    if (m_hid_dev->write(buf, buf->size()) < 0) {
        qWarning("write failed");
    }
}

void shuttle::process()
{
    quint8 jogpos=0;
    qDebug() << "Starting shuttle process";
    while (true) {
        QByteArray b(128, '0');
        if (m_hid_dev != Q_NULLPTR) {
            m_hid_dev->read(&b, b.size(), 100);

            //qDebug() << "Reading" << b << b.size();
            if (b.size() == 5)
            {
                qDebug() << "Shuttle Data received: " << hex << (quint8)b[0] << ":"
                    << hex << (quint8)b[1] << ":"
                    << hex << (quint8)b[2] << ":"
                    << hex << (quint8)b[3] << ":"
                    << hex << (quint8)b[4];
                if ((quint8)b[1] != jogpos) {
                    if ((quint8)b[1] > jogpos && ((quint8)b[1] != 0x00 && jogpos != 0xff))
                    {
                        qDebug() << "Jog UP";
                    }
                    else {
                        qDebug() << "Jog Down";
                    }
                    jogpos = (quint8)b[1];
                }
            }
        }
        else {
            QThread::yieldCurrentThread();
            QThread::msleep(100);
        }

    }
}
