#include "pskreporter.h"
#include "logcategories.h"

#include <QDataStream>
#include <QDateTime>
#include <QRandomGenerator>
#include <QtEndian>

// IPFIX (RFC 5101) wire format used by pskreporter.info.
//
//   Message header (16 bytes)
//     uint16 version            = 10  (IPFIX)
//     uint16 length             = total bytes in this UDP datagram
//     uint32 exportTime         = unix seconds
//     uint32 sequenceNumber     = increments across messages from this exporter
//     uint32 observationDomain  = random 32-bit ID, stable for the session
//
//   Template Set (set ID = 2):
//     uint16 setID = 2
//     uint16 setLength
//     {  Template record
//          uint16 templateID            (must be >= 256)
//          uint16 fieldCount
//          field specifiers...
//     } [+ pad to 4-byte boundary]
//
//   Field specifier:
//     uint16 informationElementID  (high bit = enterprise-specific)
//     uint16 fieldLength           (0xFFFF for variable length)
//     [uint32 enterpriseNumber]    (only if E-bit set)
//
//   Data Set (set ID = matching template's templateID):
//     uint16 setID
//     uint16 setLength
//     records...
//     [pad to 4-byte boundary with zeros]
//
//   Variable-length field encoding inside a record:
//     - if length <  255:  1 byte length + bytes
//     - if length >= 255:  0xFF + uint16 length + bytes
//
// All multi-byte integers are big-endian.  PSKReporter information elements
// live under enterprise number 30351.

namespace {

constexpr quint16 IPFIX_VERSION       = 10;
constexpr quint32 PSK_ENTERPRISE_PEN  = 30351;

// Custom template IDs.  Any value >= 256 works; pick distinct constants and
// keep them stable across messages so the collector can match data sets to
// the templates it has on file.
constexpr quint16 RECEIVER_TEMPLATE_ID = 0x50E2;
constexpr quint16 SENDER_TEMPLATE_ID   = 0x50E3;

// PSKReporter enterprise-specific information element IDs (high bit set on
// the wire — but we store the raw 15-bit value here and OR in 0x8000 at
// emit time).
constexpr quint16 IE_RECEIVER_CALLSIGN     = 0x8002;
constexpr quint16 IE_RECEIVER_LOCATOR      = 0x8004;
constexpr quint16 IE_RECEIVER_SOFTWARE     = 0x8008;
constexpr quint16 IE_RECEIVER_ANTENNA      = 0x8009;

constexpr quint16 IE_SENDER_CALLSIGN       = 0x8001;
constexpr quint16 IE_SENDER_LOCATOR        = 0x8003;
constexpr quint16 IE_SENDER_FREQUENCY      = 0x8005;  // 4 bytes, Hz
constexpr quint16 IE_SENDER_SNR            = 0x8006;  // 1 byte signed
constexpr quint16 IE_SENDER_MODE           = 0x8007;
// IANA-assigned standard element (no enterprise bit)
constexpr quint16 IE_FLOW_START_SECONDS    = 150;     // 4 bytes, unix sec

// Append a uint16 in big-endian order.
inline void appendU16(QByteArray &b, quint16 v)
{
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>(v & 0xFF));
}

inline void appendU32(QByteArray &b, quint32 v)
{
    b.append(static_cast<char>((v >> 24) & 0xFF));
    b.append(static_cast<char>((v >> 16) & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>(v & 0xFF));
}

inline void writeU16At(QByteArray &b, int pos, quint16 v)
{
    b[pos]     = static_cast<char>((v >> 8) & 0xFF);
    b[pos + 1] = static_cast<char>(v & 0xFF);
}

// Field specifier for an enterprise-specific information element.
inline void appendFieldSpec(QByteArray &b, quint16 ieId, quint16 length, quint32 pen)
{
    appendU16(b, ieId);   // high bit already set in the constant
    appendU16(b, length);
    appendU32(b, pen);
}

// Field specifier for a standard IANA element (no enterprise bit / PEN).
inline void appendStandardFieldSpec(QByteArray &b, quint16 ieId, quint16 length)
{
    appendU16(b, ieId);
    appendU16(b, length);
}

// Pad with zero bytes to bring the buffer length up to a 4-byte multiple.
inline void padTo4(QByteArray &b)
{
    while (b.size() % 4) b.append('\0');
}

} // namespace

PskReporter::PskReporter(QObject *parent)
    : SpotReporter(parent)
{
    socket_ = new QUdpSocket(this);
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QUdpSocket::error),
            this, &PskReporter::onSocketError);

    sendTimer_ = new QTimer(this);
    sendTimer_->setInterval(SEND_INTERVAL_MS);
    connect(sendTimer_, &QTimer::timeout, this, &PskReporter::onSendTimer);

    // 32-bit observation domain ID, stable for the lifetime of this object.
    observationDomainId_ = QRandomGenerator::global()->generate();
    if (observationDomainId_ == 0) observationDomainId_ = 1;
}

PskReporter::~PskReporter()
{
    disconnectFromService();
}

void PskReporter::setStation(const QString &callsign, const QString &grid)
{
    callsign_ = callsign.toUpper().trimmed();
    grid_ = grid.toUpper().trimmed();
}

void PskReporter::connectToService()
{
    if (callsign_.isEmpty() || grid_.isEmpty()) {
        qWarning() << "PskReporter: cannot start without callsign and grid";
        return;
    }
    if (state_ == Connected || state_ == Connecting) return;

    setState(Connecting);

    // The "connect" step is just a DNS resolve; UDP itself is connectionless.
    // Once we have an address the timer starts and the next flush will go out.
    dnsLookupId_ = QHostInfo::lookupHost(QString::fromLatin1(DEFAULT_HOST),
                                         this, &PskReporter::onLookupFinished);
    qInfo() << "PskReporter: resolving" << DEFAULT_HOST;
}

void PskReporter::disconnectFromService()
{
    if (state_ == Disconnected) return;

    // Send any buffered spots before going away — losing the last batch on
    // every shutdown would skew the heatmap.
    if (dnsResolved_ && !pendingSpots_.isEmpty())
        sendPacket();

    sendTimer_->stop();
    if (dnsLookupId_ >= 0) {
        QHostInfo::abortHostLookup(dnsLookupId_);
        dnsLookupId_ = -1;
    }
    pendingSpots_.clear();
    setState(Disconnected);
}

void PskReporter::updateFrequency(quint64 hz)
{
    currentFreq_ = hz;
}

void PskReporter::updateTx(const QString &mode, bool transmitting)
{
    Q_UNUSED(transmitting);
    if (!mode.isEmpty()) currentMode_ = mode;
}

void PskReporter::updateRxSpot(const QString &callsign, const QString &mode, int snr)
{
    updateRxSpotWithGrid(callsign, QString(), mode, snr);
}

void PskReporter::updateRxSpotWithGrid(const QString &callsign, const QString &grid,
                                       const QString &mode, int snr)
{
    if (state_ != Connected && state_ != Connecting) return;
    if (callsign.isEmpty() || currentFreq_ == 0) return;

    Spot s;
    s.callsign = callsign.toUpper().trimmed();
    s.grid = grid.toUpper().trimmed();
    s.mode = mode.isEmpty() ? currentMode_ : mode;
    s.snr = qBound(-128, snr, 127);
    s.freq = currentFreq_;
    s.timeSeconds = QDateTime::currentSecsSinceEpoch();
    pendingSpots_.append(s);
    qInfo() << "PskReporter: queued spot" << s.callsign
            << "grid=" << s.grid << "mode=" << s.mode
            << "snr=" << s.snr << "freq=" << s.freq;
}

void PskReporter::flush()
{
    if (dnsResolved_ && !pendingSpots_.isEmpty()) sendPacket();
}

void PskReporter::onLookupFinished(const QHostInfo &info)
{
    dnsLookupId_ = -1;
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        qWarning() << "PskReporter: DNS lookup failed:" << info.errorString();
        setState(Error);
        return;
    }
    // Prefer IPv4 — pskreporter.info's collector listens on both, but IPv4
    // is the documented default and what every other tool uses.
    serverAddr_ = info.addresses().first();
    for (const QHostAddress &a : info.addresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol) {
            serverAddr_ = a;
            break;
        }
    }
    dnsResolved_ = true;
    qInfo() << "PskReporter: server resolved to" << serverAddr_.toString()
            << "as" << callsign_ << grid_;

    setState(Connected);
    sendTimer_->start();
}

void PskReporter::onSendTimer()
{
    if (!dnsResolved_) return;
    if (pendingSpots_.isEmpty()) return;
    sendPacket();
}

void PskReporter::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    qWarning() << "PskReporter: socket error:" << socket_->errorString();
}

void PskReporter::setState(State s)
{
    if (state_ != s) {
        state_ = s;
        emit stateChanged(s);
    }
}

QByteArray PskReporter::encodeVarField(const QByteArray &data)
{
    QByteArray out;
    int n = data.size();
    if (n < 255) {
        out.append(static_cast<char>(n));
    } else {
        out.append(static_cast<char>(0xFF));
        appendU16(out, static_cast<quint16>(n));
    }
    out.append(data);
    return out;
}

QByteArray PskReporter::buildPacket(bool withTemplates)
{
    QByteArray packet;

    // ---- Header (16 bytes) — length and seq filled in later ----
    appendU16(packet, IPFIX_VERSION);
    appendU16(packet, 0);                       // total length (patched)
    appendU32(packet, static_cast<quint32>(QDateTime::currentSecsSinceEpoch()));
    appendU32(packet, sequence_);
    appendU32(packet, observationDomainId_);

    // ---- Template set (only when needed) ----
    if (withTemplates) {
        QByteArray tset;
        appendU16(tset, 2);          // set ID = template set
        appendU16(tset, 0);          // set length (patched)

        // Receiver template: 4 variable-length enterprise fields.
        appendU16(tset, RECEIVER_TEMPLATE_ID);
        appendU16(tset, 4);          // field count
        appendFieldSpec(tset, IE_RECEIVER_CALLSIGN, 0xFFFF, PSK_ENTERPRISE_PEN);
        appendFieldSpec(tset, IE_RECEIVER_LOCATOR,  0xFFFF, PSK_ENTERPRISE_PEN);
        appendFieldSpec(tset, IE_RECEIVER_SOFTWARE, 0xFFFF, PSK_ENTERPRISE_PEN);
        appendFieldSpec(tset, IE_RECEIVER_ANTENNA,  0xFFFF, PSK_ENTERPRISE_PEN);

        // Sender template: callsign, grid, freq, snr, mode, time.
        appendU16(tset, SENDER_TEMPLATE_ID);
        appendU16(tset, 6);          // field count
        appendFieldSpec(tset, IE_SENDER_CALLSIGN,  0xFFFF, PSK_ENTERPRISE_PEN);
        appendFieldSpec(tset, IE_SENDER_LOCATOR,   0xFFFF, PSK_ENTERPRISE_PEN);
        appendFieldSpec(tset, IE_SENDER_FREQUENCY, 4,      PSK_ENTERPRISE_PEN);
        appendFieldSpec(tset, IE_SENDER_SNR,       1,      PSK_ENTERPRISE_PEN);
        appendFieldSpec(tset, IE_SENDER_MODE,      0xFFFF, PSK_ENTERPRISE_PEN);
        appendStandardFieldSpec(tset, IE_FLOW_START_SECONDS, 4);

        padTo4(tset);
        writeU16At(tset, 2, static_cast<quint16>(tset.size()));
        packet.append(tset);
    }

    // ---- Receiver data set (one record describing this station) ----
    {
        QByteArray rset;
        appendU16(rset, RECEIVER_TEMPLATE_ID);
        appendU16(rset, 0);          // set length (patched)

        QByteArray softwareTag = QString("wfweb %1")
            .arg(QStringLiteral(WFWEB_VERSION)).toUtf8();

        rset.append(encodeVarField(callsign_.toUtf8()));
        rset.append(encodeVarField(grid_.toUtf8()));
        rset.append(encodeVarField(softwareTag));
        rset.append(encodeVarField(QByteArray()));  // antenna info — empty

        padTo4(rset);
        writeU16At(rset, 2, static_cast<quint16>(rset.size()));
        packet.append(rset);
    }

    // ---- Sender data set (one record per spot) ----
    if (!pendingSpots_.isEmpty()) {
        QByteArray sset;
        appendU16(sset, SENDER_TEMPLATE_ID);
        appendU16(sset, 0);          // set length (patched)

        for (const Spot &s : pendingSpots_) {
            sset.append(encodeVarField(s.callsign.toUtf8()));
            sset.append(encodeVarField(s.grid.toUtf8()));   // empty allowed
            appendU32(sset, static_cast<quint32>(s.freq));
            sset.append(static_cast<char>(static_cast<qint8>(s.snr)));
            sset.append(encodeVarField(s.mode.toUtf8()));
            appendU32(sset, static_cast<quint32>(s.timeSeconds));
        }

        padTo4(sset);
        writeU16At(sset, 2, static_cast<quint16>(sset.size()));
        packet.append(sset);
    }

    // Patch total message length.
    writeU16At(packet, 2, static_cast<quint16>(packet.size()));
    return packet;
}

void PskReporter::sendPacket()
{
    if (!dnsResolved_) return;

    qint64 now = QDateTime::currentSecsSinceEpoch();
    bool sendTemplates = (lastTemplateTime_ == 0)
                         || (now - lastTemplateTime_ >= TEMPLATE_REFRESH_SECS);

    QByteArray packet = buildPacket(sendTemplates);

    qint64 written = socket_->writeDatagram(packet, serverAddr_, DEFAULT_PORT);
    if (written < 0) {
        qWarning() << "PskReporter: writeDatagram failed:" << socket_->errorString();
        return;
    }
    qInfo() << "PskReporter: sent" << written << "bytes ("
            << pendingSpots_.size() << "spots, templates="
            << sendTemplates << ", seq=" << sequence_ << ")";

    sequence_ += pendingSpots_.size() + 1;     // 1 receiver record + N spots
    if (sendTemplates) lastTemplateTime_ = now;
    pendingSpots_.clear();
}
