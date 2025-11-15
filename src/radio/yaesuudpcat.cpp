#include "yaesuudpcat.h"
#include "logcategories.h"

yaesuUdpCat::yaesuUdpCat(QHostAddress local, QHostAddress remote, quint16 port)
{
    remotePort = port;
    remoteAddr = remote;
    localAddr = local;
}

void yaesuUdpCat::incomingUdp(void* buf, size_t bufLen)
{
    // First cast buf to just the header to find the packet type;
    if (bufLen <= sizeof(yaesuFrameHeader))
    {
        qInfo(logUdp()) << "Packet too small!";
        return;
    }
    yaesuFrameHeader* h = (yaesuFrameHeader*)buf;
    switch (h->msgtype)
    {
    case R2C_ConnectReply:
        // Confirmed that we are connected.
        qInfo(logUdp()) << "Got Cat Connect reply";
        state = yaesuCatPending;
        break;
    case R2C_Data:
    {
        // Now Cat is connected, connect audio
        if (state < yaesuAudioReady)
        {
            state = yaesuAudioReady;
            emit initAudio();
        }

        if (bufLen <= sizeof(yaesuR2C_CatDataFrame))
        {
            yaesuR2C_CatDataFrame* r = (yaesuR2C_CatDataFrame*)buf;
            // Ignore duplicate packets
            if (r->data.packet_id != rxPacketId)
            {
                rxPacketId = r->data.packet_id;
                emit haveCatDataFromRig(QByteArray::fromRawData((const char *)r->data.data,bufLen-sizeof(r->hdr)-4));
            }
        }
        break;
    }
    default:
        qInfo(logUdp()) << QString("Yaesu cat, unsupported packet device:%0 type:%1 len:%2").arg(h->device).arg(h->msgtype).arg(h->len);
        break;
    }
}

void yaesuUdpCat::sendCatDataToRig(QByteArray d)
{
    // We probably need to store these in an array, to handle retransmissions (once we work out how they are done)
    yaesuC2R_CatDataFrame h;
    if (d.size() <= qsizetype(sizeof(h.data.data)))
    {
        memset(&h,0,sizeof(h));

        h.hdr.device = UsbCat;
        h.hdr.msgtype = C2R_Data;
        h.hdr.len = sizeof(h) - sizeof(h.data.data) - sizeof(h.hdr) + d.size();
        h.session = session;
        h.data.packet_id = txPacketId;
        memcpy(h.data.data,d.constData(),d.size());
        outgoing((quint8*)&h,sizeof(h)-sizeof(h.data.data)+d.size());
        outgoing((quint8*)&h,sizeof(h)-sizeof(h.data.data)+d.size());
        txPacketId++;
    }
}


void yaesuUdpCat::sendHeartbeat()
{
    yaesuC2R_HeartbeatFrame f;
    memset(&f,0,sizeof(f));
    f.hdr.device=ScuLan;
    f.hdr.msgtype=C2R_HealthCheck;
    f.hdr.len=sizeof(f)-sizeof(f.hdr);
    f.session=session;
    outgoing((quint8*)&f,sizeof(yaesuC2R_HeartbeatFrame));
}

void yaesuUdpCat::sendConnect()
{
    // Send CAT connect
    yaesuResultFrame b1;
    memset(&b1,0,sizeof(b1));
    b1.hdr.device=UsbCat;
    b1.hdr.msgtype=C2R_Connect;
    b1.hdr.len=sizeof(b1)-sizeof(b1.hdr);
    b1.result = session;
    outgoing((quint8*)&b1,sizeof(b1));

    // Set CAT baud rate.
    yaesuC2R_CatRateFrame b2;
    memset(&b2,0,sizeof(b2));
    b2.hdr.device=UsbCat;
    b2.hdr.msgtype=C2R_CatRate;
    b2.session = session;
    b2.baud = 38400;
    b2.hdr.len=sizeof(b1)-sizeof(b1.hdr);
    outgoing((quint8*)&b2,sizeof(b2));
}

