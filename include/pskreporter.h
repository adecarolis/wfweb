#ifndef PSKREPORTER_H
#define PSKREPORTER_H

#include "spotreporter.h"
#include <QUdpSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QTimer>
#include <QList>

// PSK Reporter — sends FT8/FT4 (and other digital mode) decode spots to
// pskreporter.info using the IPFIX (RFC 5101) protocol over UDP port 4739.
//
// Spots are buffered and flushed on a timer (default ~5 minutes) to keep
// the reporting load low and align with how other clients (WSJT-X, JS8Call)
// behave on the network.
class PskReporter : public SpotReporter
{
    Q_OBJECT
public:
    explicit PskReporter(QObject *parent = nullptr);
    ~PskReporter() override;

    void setStation(const QString &callsign, const QString &grid) override;
    void connectToService() override;
    void disconnectFromService() override;
    void updateFrequency(quint64 hz) override;
    void updateTx(const QString &mode, bool transmitting) override;
    void updateRxSpot(const QString &callsign, const QString &mode, int snr) override;

    // Extended spot record carrying the decoded station's grid (when known).
    void updateRxSpotWithGrid(const QString &callsign, const QString &grid,
                              const QString &mode, int snr);

    // Force an immediate flush of buffered spots.  Useful before shutdown
    // or when the user explicitly disables reporting.
    void flush();

    State state() const override { return state_; }

private slots:
    void onLookupFinished(const QHostInfo &info);
    void onSendTimer();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void setState(State s);
    void sendPacket();
    QByteArray buildPacket(bool withTemplates);
    static QByteArray encodeVarField(const QByteArray &data);

    static constexpr const char *DEFAULT_HOST = "report.pskreporter.info";
    static constexpr quint16 DEFAULT_PORT = 4739;
    // Flush every 5 minutes; matches the upstream tools' default cadence.
    static constexpr int SEND_INTERVAL_MS = 5 * 60 * 1000;
    // Re-send templates at most once per hour to keep collectors in sync.
    static constexpr qint64 TEMPLATE_REFRESH_SECS = 60 * 60;

    QUdpSocket *socket_ = nullptr;
    QTimer *sendTimer_ = nullptr;
    QHostAddress serverAddr_;
    bool dnsResolved_ = false;
    int dnsLookupId_ = -1;

    QString callsign_;
    QString grid_;
    QString currentMode_;
    quint64 currentFreq_ = 0;

    quint32 sequence_ = 0;
    quint32 observationDomainId_ = 0;
    qint64 lastTemplateTime_ = 0;

    struct Spot {
        QString callsign;
        QString grid;
        QString mode;
        int snr;
        quint64 freq;
        qint64 timeSeconds;
    };
    QList<Spot> pendingSpots_;

    State state_ = Disconnected;
};

#endif // PSKREPORTER_H
