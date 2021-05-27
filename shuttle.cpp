#include "shuttle.h"
#include <QDebug>

shuttle::shuttle(QObject* parent)
    : QObject(parent), m_hid_dev(new QHidDevice())
{

    auto list = m_usb.devices();

    for (auto l : list)
    {
        qDebug() << "   Device:" << QString(l);
    }

    m_send.append(static_cast<char>(0x81u));
    m_send.append(static_cast<char>(0x01u));

    if (this->openDevice()) {
        qInfo("Device open!");
        //this->write(&m_send);
        qDebug() << "Manufacturer:" << m_hid_dev->manufacturer();
        qDebug() << "Product:" << m_hid_dev->product();
    }
    else {
        qWarning("Could not open device!");
    }
}

shuttle::~shuttle()
{
    this->closeDevice();
    m_hid_dev->deleteLater();
}


bool shuttle::openDevice()
{
    qDebug("Opening");

    return m_hid_dev->open(0x0b33, 0x0020);
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

