#ifndef APRSPROCESSOR_H
#define APRSPROCESSOR_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

/*
 *  AprsProcessor
 *
 *  Stateful APRS layer that sits above DireWolfProcessor.  Filters UI
 *  frames, parses position-bearing payloads (uncompressed, compressed,
 *  MIC-E), and maintains an in-memory station database.  Emits delta
 *  events so the web UI can render a live stations list, and snapshots
 *  on demand for new clients.
 *
 *  Beacon scheduler lives here too: a periodic QTimer fires every
 *  intervalSec and emits txBeaconRequested with src/dst/path/info; the
 *  webserver maps that into its existing packetTx flow (key PTT, hand
 *  the monitor string to DireWolfProcessor).  Living in the daemon
 *  means beacons keep firing even with no browser attached.
 *
 *  Threading: meant to live in the webserver thread.  onRxFrame() is
 *  invoked via Qt::QueuedConnection from DireWolfProcessor.
 */
class AprsProcessor : public QObject
{
    Q_OBJECT
public:
    struct Station {
        QString src;
        double  lat = 0.0;
        double  lon = 0.0;
        QChar   symTable = '/';
        QChar   symCode  = '.';
        QString comment;
        QStringList path;
        qint64  lastHeardMs = 0;
        quint32 count = 0;
    };

    explicit AprsProcessor(QObject *parent = nullptr);

    QJsonObject snapshot() const;

    // Returns true if `info` parses as a position report and fills out
    // outLat / outLon / outSymTable / outSymCode / outComment.  `dst`
    // is the AX.25 destination callsign — needed for MIC-E latitude.
    static bool parsePosition(const QString &dst,
                              const QString &info,
                              double &outLat,
                              double &outLon,
                              QChar  &outSymTable,
                              QChar  &outSymCode,
                              QString &outComment);

    // Build a standard uncompressed-position UI frame information field:
    //   "!DDMM.mmN/DDDMM.mmW<sym>comment"
    // Comment is truncated to 43 chars (APRS spec).
    static QByteArray buildPositionInfo(double lat, double lon,
                                        QChar symTable, QChar symCode,
                                        const QString &comment);

public slots:
    // Connected to DireWolfProcessor::rxFrameDecoded.  Filters UI frames
    // and updates the station map.  Non-position UI frames (status,
    // messages, telemetry) are ignored for v1.
    void onRxFrame(int chan, QJsonObject frame);

    // Configure the periodic beacon.  When `enabled` and `intervalSec`>0,
    // the timer fires every intervalSec and emits txBeaconRequested.
    void setBeaconConfig(bool enabled, int intervalSec,
                         const QString &src,
                         double lat, double lon,
                         QChar symTable, QChar symCode,
                         const QString &comment,
                         const QStringList &path);

    // Send a single beacon now using the supplied fields (does not
    // change periodic-beacon state).  Just emits txBeaconRequested.
    void txBeaconNow(const QString &src,
                     double lat, double lon,
                     QChar symTable, QChar symCode,
                     const QString &comment,
                     const QStringList &path);

    // Forget all heard stations.
    void clearStations();

signals:
    // Emitted on every position parse — even if the station was already
    // known.  Web UI uses this both to insert new rows and to refresh
    // last-heard timestamps for existing ones.
    void stationUpdated(QJsonObject station);
    void stationsCleared();

    // Beacon TX request.  Webserver wires this to its existing packet
    // TX flow (which keys PTT and calls DireWolfProcessor::transmitFrame).
    void txBeaconRequested(QString src, QString dst,
                           QStringList path, QString info);

private slots:
    void onBeaconTimer();

private:
    QJsonObject stationToJson(const Station &s) const;

    QHash<QString, Station> stations_;

    QTimer  *beaconTimer_ = nullptr;
    bool     beaconEnabled_ = false;
    int      beaconIntervalSec_ = 600;
    QString  beaconSrc_;
    double   beaconLat_ = 0.0;
    double   beaconLon_ = 0.0;
    QChar    beaconSymTable_ = '/';
    QChar    beaconSymCode_  = '.';
    QString  beaconComment_;
    QStringList beaconPath_;
};

#endif // APRSPROCESSOR_H
