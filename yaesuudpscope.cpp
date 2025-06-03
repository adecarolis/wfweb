#include "yaesuudpscope.h"
#include "logcategories.h"

yaesuUdpScope::yaesuUdpScope(QHostAddress local, QHostAddress remote, quint16 port)
{
    remotePort = port;
    remoteAddr = remote;
    localAddr = local;
}

void yaesuUdpScope::incomingUdp(void* buf, size_t bufLen)
{
    // First cast buf to just the header to find the packet type;
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
        qInfo(logUdp()) << "Got Scope Connect reply";
        break;
    case R2C_Data:
        // Is this scope data?
        if (bufLen == sizeof(yaesuR2C_ScopeDataFrame))
        {
            yaesuR2C_ScopeDataFrame* r = (yaesuR2C_ScopeDataFrame*)buf;
            emit haveScopeData(QByteArray((char *)r->data,sizeof(r->data)));
        }
        break;
    default:
        qInfo(logUdp()) << QString("Yaesu scope, unsupported packet device:%0 type:%1 len:%2").arg(h->device).arg(h->msgtype).arg(h->len);
        break;
    }
}

void yaesuUdpScope::sendHeartbeat()
{
    yaesuC2R_HeartbeatFrame f;
    memset(&f,0,sizeof(f));
    f.hdr.device=Scope;
    f.hdr.msgtype=C2R_HealthCheck;
    f.hdr.len=sizeof(f)-sizeof(f.hdr);
    f.session=session;
    outgoing((quint8*)&f,sizeof(yaesuC2R_HeartbeatFrame));
}
void yaesuUdpScope::sendConnect()
{
    qInfo(logUdp()) << "Sending request to start scope";
    yaesuResultFrame d1;
    memset(&d1,0,sizeof(d1));
    d1.hdr.device=Scope;
    d1.hdr.msgtype=C2R_Connect;
    d1.hdr.len=sizeof(d1)-sizeof(d1.hdr);
    d1.result = session;
    outgoing((quint8*)&d1,sizeof(d1));
}

