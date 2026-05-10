#include "extrigctld.h"
#include "extslot.h"

#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QByteArray>
#include <QStringList>

namespace {

// Hamlib mode bitmask for IC-7300-like rig: AM|CW|USB|LSB|RTTY|FM|CWR|RTTYR|PKTLSB|PKTUSB
//   AM=0x1 CW=0x2 USB=0x4 LSB=0x8 RTTY=0x10 FM=0x20 CWR=0x80 RTTYR=0x100
//   PKTLSB=0x400 PKTUSB=0x800 → 0xD9F
constexpr quint64 kModeMask = 0xD9FULL;

// VFO bitmask: VFO_A | VFO_B in newer Hamlib (each is a 24-bit-shifted flag).
// JS8Call accepts whatever we report here as long as we're internally consistent.
constexpr quint64 kVfoList = 0x3000000ULL;

// Parse a hertz value from a string. Hamlib accepts integer Hz and "1.234e+06"
// forms; for our subset we handle plain integers and floats.
bool parseHz(const QString& s, quint64& out)
{
    bool ok = false;
    if (s.contains('.') || s.contains('e') || s.contains('E')) {
        double d = s.toDouble(&ok);
        if (!ok || d < 0) return false;
        out = (quint64)(d + 0.5);
        return true;
    }
    out = s.toULongLong(&ok);
    return ok;
}

} // namespace

ExtRigctld::ExtRigctld(ExtSlot* slot, QObject* parent)
    : QObject(parent), slot(slot)
{
}

ExtRigctld::~ExtRigctld()
{
    stop();
}

bool ExtRigctld::listen(quint16 port)
{
    server = new QTcpServer(this);
    connect(server, &QTcpServer::newConnection, this, &ExtRigctld::onNewConnection);
    if (!server->listen(QHostAddress::LocalHost, port)) {
        qWarning() << "extrigctld: listen on" << port << "failed:" << server->errorString();
        return false;
    }
    qInfo() << "extrigctld: slot" << slot->name() << "listening on 127.0.0.1:" << port;
    return true;
}

void ExtRigctld::stop()
{
    if (server) {
        server->close();
        for (auto it = buffers.begin(); it != buffers.end(); ++it) {
            it.key()->disconnectFromHost();
        }
        buffers.clear();
        server->deleteLater();
        server = nullptr;
    }
}

void ExtRigctld::onNewConnection()
{
    while (QTcpSocket* s = server->nextPendingConnection()) {
        connect(s, &QTcpSocket::readyRead,    this, &ExtRigctld::onReadyRead);
        connect(s, &QTcpSocket::disconnected, this, &ExtRigctld::onDisconnected);
        buffers.insert(s, QByteArray());
    }
}

void ExtRigctld::onDisconnected()
{
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    buffers.remove(s);
    s->deleteLater();
}

void ExtRigctld::onReadyRead()
{
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    QByteArray& buf = buffers[s];
    buf.append(s->readAll());

    // Dispatch one full line at a time. NetRigctl uses LF or CRLF.
    while (true) {
        int nl = buf.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = buf.left(nl);
        buf.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;
        handleLine(s, line);
    }
}

void ExtRigctld::writeData(QTcpSocket* s, const QByteArray& data)
{
    s->write(data);
    s->flush();
}

void ExtRigctld::handleLine(QTcpSocket* s, const QByteArray& line)
{
    // Extended-mode prefix: '\' (long form like "\set_freq") or '+' (short
    // form with extended response). We reply identically — get commands
    // return raw values, set commands return RPRT — which is what NetRigctl
    // expects for the bare protocol. JS8Call works fine with this.
    QString text = QString::fromUtf8(line).trimmed();
    if (text.startsWith('+')) text.remove(0, 1);
    bool extended = false;
    if (text.startsWith('\\')) {
        text.remove(0, 1);
        extended = true;
    }
    if (text.isEmpty()) return;

    QStringList parts = text.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;
    const QString& cmd = parts[0];

    Q_UNUSED(extended)
    auto sendOk   = [&]() { writeData(s, QByteArray("RPRT 0\n")); };
    auto sendErr  = [&](int code) {
        writeData(s, QByteArray("RPRT ") + QByteArray::number(code) + "\n");
    };
    auto sendBody = [&](const QString& body) { writeData(s, body.toUtf8()); };

    // --- queries ---
    if (cmd == "f" || cmd == "get_freq") {
        sendBody(QString::number(slot->freq()) + "\n");
        return;
    }
    if (cmd == "m" || cmd == "get_mode") {
        sendBody(ExtSlot::modeToName(slot->mode()) + "\n" +
                 QString::number(slot->modeWidth()) + "\n");
        return;
    }
    if (cmd == "t" || cmd == "get_ptt") {
        sendBody(QString::number(slot->isTransmitting() ? 1 : 0) + "\n");
        return;
    }
    if (cmd == "v" || cmd == "get_vfo") {
        sendBody(QString("VFOA\n"));
        return;
    }
    if (cmd == "s" || cmd == "get_split_vfo") {
        // First line: split flag (0/1). Second line: TX VFO when split.
        sendBody(QString::number(slot->splitEnabled() ? 1 : 0) + "\n" +
                 QString("VFOB\n"));
        return;
    }
    if (cmd == "i" || cmd == "get_split_freq") {
        sendBody(QString::number(slot->splitTxFreq()) + "\n");
        return;
    }
    if (cmd == "x" || cmd == "get_split_mode") {
        sendBody(ExtSlot::modeToName(slot->splitMode()) + "\n" +
                 QString::number(slot->splitModeWidth()) + "\n");
        return;
    }
    if (cmd == "get_powerstat") {
        sendBody(QString("1\n"));   // powered on
        return;
    }
    if (cmd == "chk_vfo") {
        // 0 = caller does NOT need to pass an explicit VFO arg with set commands.
        sendBody(QString("CHKVFO 0\n"));
        return;
    }
    if (cmd == "dump_state") {
        // Emitted as multiple lines. Fields chosen to look like an Icom HF rig
        // (similar to IC-7300) — JS8Call only consults this for general
        // capability flags, not strict freq-range enforcement.
        QString out;
        out += "1\n";                               // protocol version
        out += "2\n";                               // model id (NetRigctl=2 — fine for a fake rigctl)
        out += "0\n";                               // legacy region
        // RX ranges (start_hz end_hz mode_mask low_pwr high_pwr vfo_mask ant_mask)
        out += "30000.000000 60000000.000000 0x" + QString::number(kModeMask, 16) +
               " -1 -1 0x" + QString::number(kVfoList, 16) + " 0x0\n";
        out += "0 0 0 0 0 0 0\n";                   // RX terminator
        // TX ranges — ham bands only
        struct { quint64 lo, hi; } bands[] = {
            { 1800000ULL,    2000000ULL },   // 160m
            { 3500000ULL,    4000000ULL },   // 80m
            { 5250000ULL,    5450000ULL },   // 60m
            { 7000000ULL,    7300000ULL },   // 40m
            { 10100000ULL,  10150000ULL },   // 30m
            { 14000000ULL,  14350000ULL },   // 20m
            { 18068000ULL,  18168000ULL },   // 17m
            { 21000000ULL,  21450000ULL },   // 15m
            { 24890000ULL,  24990000ULL },   // 12m
            { 28000000ULL,  29700000ULL },   // 10m
            { 50000000ULL,  54000000ULL },   // 6m
        };
        for (const auto& b : bands) {
            out += QString::number(b.lo) + ".000000 " +
                   QString::number(b.hi) + ".000000 0x" +
                   QString::number(kModeMask, 16) +
                   " 2000 100000 0x" +
                   QString::number(kVfoList, 16) + " 0x0\n";
        }
        out += "0 0 0 0 0 0 0\n";                   // TX terminator
        // Tuning steps (mode_mask hz)
        out += "0x" + QString::number(kModeMask, 16) + " 1\n";
        out += "0x" + QString::number(kModeMask, 16) + " 10\n";
        out += "0x" + QString::number(kModeMask, 16) + " 100\n";
        out += "0 0\n";                             // tuning step terminator
        // Filters (mode_mask width_hz)
        out += "0xC0F 3000\n";                      // SSB/PKT 3 kHz
        out += "0xC0F 2400\n";
        out += "0xC0F 1800\n";
        out += "0x82 1200\n";                       // CW
        out += "0x82 500\n";
        out += "0x82 200\n";
        out += "0x110 2400\n";                      // RTTY
        out += "0x110 500\n";
        out += "0x110 250\n";
        out += "0x20 15000\n";                      // FM
        out += "0x20 10000\n";
        out += "0x1 6000\n";                        // AM
        out += "0 0\n";                             // filter terminator
        out += "9990\n";                            // max_rit
        out += "9990\n";                            // max_xit
        out += "10000\n";                           // max_ifshift
        out += "0\n";                               // announces
        out += "0\n";                               // preamps (none)
        out += "0\n";                               // attenuators (none)
        // has_get/set_func, has_get/set_level, has_get/set_parm — minimal
        out += "0x0\n";
        out += "0x0\n";
        out += "0x0\n";
        out += "0x0\n";
        out += "0x0\n";
        out += "0x0\n";
        // Extended trailer (only emitted after chk_vfo is called by hamlib)
        out += "vfo_ops=0xff\n";
        out += "ptt_type=0x1\n";
        out += "has_set_vfo=0x1\n";
        out += "has_get_vfo=0x1\n";
        out += "has_set_freq=0x1\n";
        out += "has_get_freq=0x1\n";
        out += "has_set_conf=0x0\n";
        out += "has_get_conf=0x0\n";
        out += "has_power2mW=0x0\n";
        out += "has_mW2power=0x0\n";
        out += "timeout=0x3e8\n";                   // 1000ms
        out += "done\n";
        sendBody(out);
        return;
    }

    // --- mutators ---
    if ((cmd == "F" || cmd == "set_freq") && parts.size() >= 2) {
        quint64 hz;
        if (!parseHz(parts[1], hz)) { sendErr(-1); return; }
        slot->setFreq(hz);
        sendOk();
        return;
    }
    if ((cmd == "M" || cmd == "set_mode") && parts.size() >= 2) {
        quint8 m = ExtSlot::modeFromName(parts[1]);
        quint32 w = (parts.size() >= 3) ? parts[2].toUInt() : 0;
        slot->setMode(m, w);
        sendOk();
        return;
    }
    if ((cmd == "T" || cmd == "set_ptt") && parts.size() >= 2) {
        slot->setPtt(parts[1].toInt() != 0);
        sendOk();
        return;
    }
    if ((cmd == "V" || cmd == "set_vfo") && parts.size() >= 2) {
        // Single-VFO model — accept any but reflect VFOA in get.
        sendOk();
        return;
    }
    if ((cmd == "S" || cmd == "set_split_vfo") && parts.size() >= 2) {
        bool en = parts[1].toInt() != 0;
        slot->setSplit(en, slot->splitTxFreq());
        sendOk();
        return;
    }
    if ((cmd == "I" || cmd == "set_split_freq") && parts.size() >= 2) {
        quint64 hz;
        if (!parseHz(parts[1], hz)) { sendErr(-1); return; }
        slot->setSplit(slot->splitEnabled(), hz);
        sendOk();
        return;
    }
    if ((cmd == "X" || cmd == "set_split_mode") && parts.size() >= 2) {
        quint8 m = ExtSlot::modeFromName(parts[1]);
        quint32 w = (parts.size() >= 3) ? parts[2].toUInt() : 0;
        slot->setSplitMode(m, w);
        sendOk();
        return;
    }
    if (cmd == "set_powerstat" && parts.size() >= 2) {
        // Don't actually power-cycle the virtual rig; just ack.
        sendOk();
        return;
    }
    if (cmd == "q" || cmd == "Q") {
        s->disconnectFromHost();
        return;
    }

    // Unknown / unsupported.
    qDebug() << "extrigctld: slot" << slot->name() << "unhandled command:" << text;
    sendErr(-11);   // RIG_ENAVAIL — Feature not available
}
