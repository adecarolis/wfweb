#include "yaesuudpcontrol.h"
#include "logcategories.h"

yaesuUdpControl::yaesuUdpControl(udpPreferences prefs, QHostAddress local, QHostAddress remote) :
    prefs(prefs)
{
    // These belong to the base class:
    remotePort = prefs.controlLANPort;
    remoteAddr = remote;
    localAddr = local;

    qInfo(logUdp()).noquote() << QString("Starting yaesudpControl Radio IP %0, User: %1").arg(remoteAddr.toString(),prefs.username);
}

yaesuUdpControl::~yaesuUdpControl()
{
    if (heartbeat != Q_NULLPTR)
    {
        heartbeat->stop();
        delete heartbeat;
    }
    sendDisconnect();

    qDebug(logUdp()) << "Yaesu udpControl successfully closed";
}


void yaesuUdpControl::incomingUdp(void* buf, size_t bufLen)
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
        case R2C_LoginReply:
        {
            // If we got a reply, then we are logged-in I think!
            if (bufLen == sizeof(yaesuR2C_LoginReplyFrame))
            {
                yaesuR2C_LoginReplyFrame* r = (yaesuR2C_LoginReplyFrame*)buf;
                qInfo(logUdp()) << "Got login reply, session:" << r->session << "vala" << r->vala << "valb" << r->valb << "valc" << r->valc;
                this->session = r->session;
                emit haveSession(r->session);
                // now we are connected, send a data packet:
                yaesuControlCommandFrame c;
                memset(&c,0,sizeof(c));
                c.hdr.device = ScuLan;
                c.hdr.msgtype = C2R_Data;
                c.hdr.len=sizeof(c)-sizeof(c.hdr);
                c.session = this->session;
                QString command = "putfile catcmd_table";
                memcpy(&c.text,command.toLocal8Bit(),command.size());
                outgoing((quint8*)&c,sizeof(c));
                if (heartbeat == Q_NULLPTR)
                {
                    heartbeat = new QTimer(this);
                    connect(heartbeat, SIGNAL(timeout()), this, SLOT(sendHeartbeat()));
                    heartbeat->start(10);
                }

            }
            break;
        }
        case R2C_LogoutReply:
            break;
        case R2C_OpenReply:

            switch (state)
            {
            case yaesuConnect:
                state = yaesuCatSent;
                break;
            case yaesuCatSent:
                state = yaesuAudioSent;
                break;
            case yaesuAudioSent:
                heartbeat->setInterval(1000);
                emit initCat();
                state = yaesuScopeSent;
                break;
            default:
                break;
            }


            break;
        case R2C_ConnectReply:
            // Confirmed that we are connected.
            qInfo(logUdp()) << "Got Connect reply";

            break;
        case R2C_Data:
        {
            // This is variable length, so just check the session ID.

            if (bufLen <= sizeof(yaesuControlCommandFrame))
            {
                yaesuControlCommandFrame* c = (yaesuControlCommandFrame*)buf;
                if (c->session == this->session)
                {
                    if (!strncmp(c->text,"Ready",5))
                    {
                        // Got ready!
                        QString cmd = "16,3,SPM\r\n9,3,SPW\r\n9,3,SPR\r\n9,3,SPw\r\n9,3,SPr\r\n427,3,SPA\r\n7,3,SPN\r\n229,2,E0\r\n10674,2,E8\r\n21,8,EX040101\r\n";
                        yaesuControlCommandFrameB b;
                        b.hdr.device = ScuLan;
                        b.hdr.msgtype = C2R_Data;
                        b.hdr.len=sizeof(b)-sizeof(b.hdr)-sizeof(b.text)+cmd.size();
                        memcpy(&b.text,cmd.toLocal8Bit(),cmd.size());
                        b.session = this->session;
                        b.reserved=1;
                        b.vala=0;
                        b.valb=0x67;
                        b.valc=0x100;
                        outgoing((quint8*)&b,sizeof(b)-sizeof(b.text)+cmd.size());
                    }
                    else if (!strncmp(c->text,"Receive",7))
                    {
                        // Send Cat Open
                        state = yaesuConnect;
                    }
                }
            }
            break;
        }
        case R2C_CloseReply:
            break;
        case R2C_HealthReply:
            break;
        default:
            qInfo(logUdp()) << QString("Yaesu control, unsupported packet device:%0 type:%1 len:%2").arg(h->device).arg(h->msgtype).arg(h->len);
            break;
    }

}



void yaesuUdpControl::sendHeartbeat()
{

    if (state == yaesuConnect)
    {
        // Send Cat Open
        yaesuC2R_CatOpenFrame b;
        memset(&b,0,sizeof(b));
        b.hdr.device = UsbCat;
        b.hdr.msgtype = C2R_Open;
        b.hdr.len = sizeof(b)-sizeof(b.hdr);
        b.session = this->session;
        b.netPort = prefs.serialLANPort;
        b.baud = 115200;
        outgoing((quint8*)&b,sizeof(b));
    }
    else if (state == yaesuCatSent)
    {
        // Send Audio open
        yaesuC2R_DeviceOpenFrame c;
        memset(&c,0,sizeof(c));
        c.hdr.device=UsbAudio;
        c.hdr.msgtype = C2R_Open;
        c.hdr.len = sizeof(c)-sizeof(c.hdr);
        c.session = this->session;
        c.netPort = prefs.audioLANPort;
        outgoing((quint8*)&c,sizeof(c));
    }
    else if (state == yaesuAudioSent)
    {
        // Send Scope open
        yaesuC2R_DeviceOpenFrame d;
        memset(&d,0,sizeof(d));
        d.hdr.device=Scope;
        d.hdr.msgtype = C2R_Open;
        d.hdr.len = sizeof(d)-sizeof(d.hdr);
        d.session = this->session;
        d.netPort = prefs.scopeLANPort;
        outgoing((quint8*)&d,sizeof(d));
    }
    else if (state != yaesuStart)
    {
        yaesuC2R_HeartbeatFrame f;
        memset(&f,0,sizeof(f));
        f.hdr.device=ScuLan;
        f.hdr.msgtype=C2R_HealthCheck;
        f.hdr.len=sizeof(f)-sizeof(f.hdr);
        f.session=session;
        outgoing((quint8*)&f,sizeof(yaesuC2R_HeartbeatFrame));
    }
}


void yaesuUdpControl::sendConnect()
{
    // OEM software sends 5 heartbeat packets first (presuably to wake-up SCU-LAN10?)
    for (int i=0;i<5;i++) {
        sendHeartbeat();
    }

    yaesuC2R_LoginFrame l;
    memset(&l,0,sizeof(l));
    l.hdr.device=ScuLan;
    l.hdr.msgtype=C2R_Login;
    l.hdr.len=sizeof(l)-sizeof(l.hdr);
    memcpy(l.username,prefs.username.toLocal8Bit(),qMin((int)prefs.password.size(),(int)sizeof(l.username)));
    memcpy(l.pw,prefs.password.toLocal8Bit(),qMin((int)prefs.password.size(),(int)sizeof(l.pw)));
    outgoing((quint8*)&l,sizeof(yaesuC2R_LoginFrame));
}

void yaesuUdpControl::sendDisconnect()
{
    yaesuC2R_LogoutFrame l;
    memset(&l,0,sizeof(l));
    l.hdr.device=ScuLan;
    l.hdr.msgtype=C2R_Logout;
    l.hdr.len=sizeof(l)-sizeof(l.hdr);
    l.session=this->session;
    outgoing((quint8*)&l,sizeof(l));
}
