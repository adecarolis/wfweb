#include "aprsprocessor.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QtMath>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(logWebServer)

AprsProcessor::AprsProcessor(QObject *parent)
    : QObject(parent)
{
    beaconTimer_ = new QTimer(this);
    beaconTimer_->setTimerType(Qt::VeryCoarseTimer);
    connect(beaconTimer_, &QTimer::timeout, this, &AprsProcessor::onBeaconTimer);
}

QJsonObject AprsProcessor::stationToJson(const Station &s) const
{
    QJsonObject j;
    j["src"]      = s.src;
    j["lat"]      = s.lat;
    j["lon"]      = s.lon;
    j["symTable"] = QString(s.symTable);
    j["symCode"]  = QString(s.symCode);
    j["comment"]  = s.comment;
    QJsonArray pa;
    for (const QString &p : s.path) pa.append(p);
    j["path"]     = pa;
    j["lastHeard"] = s.lastHeardMs;
    j["count"]    = (qint64)s.count;
    return j;
}

QJsonObject AprsProcessor::snapshot() const
{
    QJsonObject obj;
    obj["type"] = "aprsSnapshot";
    QJsonArray arr;
    for (auto it = stations_.constBegin(); it != stations_.constEnd(); ++it) {
        arr.append(stationToJson(it.value()));
    }
    obj["stations"] = arr;
    return obj;
}

void AprsProcessor::clearStations()
{
    stations_.clear();
    emit stationsCleared();
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

namespace {

// Strip an optional 7-byte APRS timestamp (HHMMSSh / DDHHMMz / DDHHMM/) from
// the head of `s`.  Returns the remainder if a timestamp was present, else
// returns the original string unchanged.
QString stripTimestamp(const QString &s)
{
    if (s.size() < 7) return s;
    QChar suffix = s.at(6);
    if (suffix != 'z' && suffix != '/' && suffix != 'h') return s;
    for (int i = 0; i < 6; i++) {
        if (!s.at(i).isDigit()) return s;
    }
    return s.mid(7);
}

bool parseUncompressed(const QString &body, double &lat, double &lon,
                       QChar &symTable, QChar &symCode, QString &comment)
{
    // body must start with: DDMM.mmN/S<sym1>DDDMM.mmE/W<sym2><comment>
    if (body.size() < 19) return false;

    // Fixed-width slots — easy to validate digit-by-digit.
    QString latStr = body.mid(0, 8);   // "DDMM.mmH"
    QChar   latHem = latStr.at(7);
    QChar   sym1   = body.at(8);
    QString lonStr = body.mid(9, 9);   // "DDDMM.mmH"
    QChar   lonHem = lonStr.at(8);
    QChar   sym2   = body.at(18);

    if (latHem != 'N' && latHem != 'S') return false;
    if (lonHem != 'E' && lonHem != 'W') return false;

    // Spaces are allowed in low positions for "position ambiguity"; treat
    // them as zeros.
    auto digOrZero = [](QChar c) -> int {
        if (c == ' ') return 0;
        if (c.isDigit()) return c.digitValue();
        return -1;
    };

    int latDeg = digOrZero(latStr.at(0)) * 10 + digOrZero(latStr.at(1));
    int latMin = digOrZero(latStr.at(2)) * 10 + digOrZero(latStr.at(3));
    if (latStr.at(4) != '.') return false;
    int latMinFrac = digOrZero(latStr.at(5)) * 10 + digOrZero(latStr.at(6));
    if (latDeg < 0 || latMin < 0 || latMinFrac < 0) return false;

    int lonDeg = digOrZero(lonStr.at(0)) * 100
               + digOrZero(lonStr.at(1)) * 10
               + digOrZero(lonStr.at(2));
    int lonMin = digOrZero(lonStr.at(3)) * 10 + digOrZero(lonStr.at(4));
    if (lonStr.at(5) != '.') return false;
    int lonMinFrac = digOrZero(lonStr.at(6)) * 10 + digOrZero(lonStr.at(7));
    if (lonDeg < 0 || lonMin < 0 || lonMinFrac < 0) return false;

    lat = latDeg + (latMin + latMinFrac / 100.0) / 60.0;
    if (latHem == 'S') lat = -lat;
    lon = lonDeg + (lonMin + lonMinFrac / 100.0) / 60.0;
    if (lonHem == 'W') lon = -lon;

    symTable = sym1;
    symCode  = sym2;
    comment  = body.mid(19).trimmed();
    return true;
}

// Compressed position: 13 bytes (sym_table + 4 lat + 4 lon + sym_code +
// 2 cs + 1 type), then comment.  All lat/lon bytes are Base91 (offset 33).
bool parseCompressed(const QString &body, double &lat, double &lon,
                     QChar &symTable, QChar &symCode, QString &comment)
{
    if (body.size() < 13) return false;

    auto inB91 = [](QChar c) {
        ushort u = c.unicode();
        return u >= 0x21 && u <= 0x7b;
    };

    // Sym table is `/` or `\` for std/alt, or alnum for an overlay.
    QChar sym1 = body.at(0);
    if (!(sym1 == '/' || sym1 == '\\' || sym1.isLetterOrNumber())) return false;

    for (int i = 1; i <= 9; i++) {
        if (!inB91(body.at(i))) return false;
    }

    auto b91 = [](QChar c) -> int { return c.unicode() - 33; };

    int y = b91(body.at(1)) * 91 * 91 * 91
          + b91(body.at(2)) * 91 * 91
          + b91(body.at(3)) * 91
          + b91(body.at(4));
    int x = b91(body.at(5)) * 91 * 91 * 91
          + b91(body.at(6)) * 91 * 91
          + b91(body.at(7)) * 91
          + b91(body.at(8));

    lat =  90.0 - y / 380926.0;
    lon = -180.0 + x / 190463.0;

    symTable = sym1;
    symCode  = body.at(9);
    comment  = body.mid(13).trimmed();
    return true;
}

// MIC-E: latitude is encoded in the AX.25 destination callsign (6 chars).
// `info` first byte after DTI is meaningless for parse purposes; the real
// shape is:
//   info[0] = lon degrees + 28 (offset)
//   info[1] = lon minutes + 28
//   info[2] = lon hundredths of minutes + 28
//   info[3..5] = speed/course (encoded)
//   info[6] = symbol code
//   info[7] = symbol table id
// `dti` (data type ID) has already been consumed before calling.
bool parseMicE(const QString &dst, const QString &micEPayload,
               double &lat, double &lon,
               QChar &symTable, QChar &symCode, QString &comment)
{
    if (dst.size() < 6 || micEPayload.size() < 8) return false;

    // First six chars of dst encode lat digits + N/S/E/W flags + offsets.
    // Each char is either a digit, a letter (P-Z, J-K, L), or specific
    // symbols.  The decode rules from APRS spec 1.0 chapter 10:
    //
    //   chr   meaning             digit   N/S    Long offset    W/E
    //   0-9   lat digit           0-9     S      0              E
    //   A-J   custom message bit  0-9     -      -              -
    //   K     ambiguity                   -      -              -
    //   L     space                       S      0              E
    //   P-Y   standard message    0-9     N      +100           W
    //   Z     ambiguity, std msg          N      +100           W
    //
    // We only need the lat digits and the N/S, lon-offset, W/E flags.
    int latDig[6];
    bool nFlag = false, wFlag = false, lonOffset100 = false;
    for (int i = 0; i < 6; i++) {
        QChar c = dst.at(i);
        ushort u = c.unicode();
        if (u >= '0' && u <= '9') {
            latDig[i] = u - '0';
        } else if (u >= 'A' && u <= 'J') {
            latDig[i] = u - 'A';
        } else if (c == 'K' || c == 'L') {
            latDig[i] = 0; // ambiguity / space → treat as 0
        } else if (u >= 'P' && u <= 'Y') {
            latDig[i] = u - 'P';
        } else if (c == 'Z') {
            latDig[i] = 0;
        } else {
            return false;
        }
        // Flags only set by chars in positions 3 (N/S), 4 (lon offset),
        // 5 (W/E) per spec.
        if (i == 3 && (u >= 'P' && u <= 'Z')) nFlag = true;
        if (i == 4 && (u >= 'P' && u <= 'Z')) lonOffset100 = true;
        if (i == 5 && (u >= 'P' && u <= 'Z')) wFlag = true;
    }

    int latDeg = latDig[0] * 10 + latDig[1];
    int latMin = latDig[2] * 10 + latDig[3];
    int latMinFrac = latDig[4] * 10 + latDig[5];
    lat = latDeg + (latMin + latMinFrac / 100.0) / 60.0;
    if (!nFlag) lat = -lat;

    // Longitude from info field.
    int lonDeg = (uchar)micEPayload.at(0).toLatin1() - 28;
    int lonMin = (uchar)micEPayload.at(1).toLatin1() - 28;
    int lonMinFrac = (uchar)micEPayload.at(2).toLatin1() - 28;
    if (lonOffset100) lonDeg += 100;
    // Spec deg ranges: 0-179 maps as 0-179 raw; values 60-69 invalid.
    if (lonDeg < 0 || lonDeg > 179) return false;
    if (lonMin < 0 || lonMin >= 60) return false;
    if (lonMinFrac < 0 || lonMinFrac > 99) return false;
    lon = lonDeg + (lonMin + lonMinFrac / 100.0) / 60.0;
    if (wFlag) lon = -lon;

    symCode  = micEPayload.at(6);
    symTable = micEPayload.at(7);
    // MIC-E "telemetry" / status text starts at byte 8.  Skip the
    // optional leading speed/course already consumed in [3..5]; treat
    // the rest as comment.
    comment = (micEPayload.size() > 8) ? micEPayload.mid(8).trimmed() : QString();
    return true;
}

} // anonymous

bool AprsProcessor::parsePosition(const QString &dst,
                                  const QString &info,
                                  double &outLat,
                                  double &outLon,
                                  QChar  &outSymTable,
                                  QChar  &outSymCode,
                                  QString &outComment)
{
    if (info.isEmpty()) return false;
    QChar dti = info.at(0);

    // Standard uncompressed / compressed payloads.
    if (dti == '!' || dti == '=' || dti == '/' || dti == '@') {
        QString body = info.mid(1);
        if (dti == '/' || dti == '@') body = stripTimestamp(body);
        if (body.isEmpty()) return false;

        // Compressed positions begin with a sym table (`/`, `\`, or an
        // alphanumeric overlay) followed by Base91 chars.  Uncompressed
        // begins with a digit (latitude DD).
        QChar first = body.at(0);
        if (first.isDigit() || first == ' ') {
            return parseUncompressed(body, outLat, outLon,
                                     outSymTable, outSymCode, outComment);
        }
        return parseCompressed(body, outLat, outLon,
                               outSymTable, outSymCode, outComment);
    }

    // MIC-E uses one of these DTIs.  0x1c/0x1d are "current" / "old"
    // GPS variants — same wire format, so accept them all.
    if (dti == '\'' || dti == '`'
        || dti.unicode() == 0x1c || dti.unicode() == 0x1d) {
        QString body = info.mid(1);
        return parseMicE(dst, body, outLat, outLon,
                         outSymTable, outSymCode, outComment);
    }

    return false;
}

QByteArray AprsProcessor::buildPositionInfo(double lat, double lon,
                                            QChar symTable, QChar symCode,
                                            const QString &comment)
{
    bool latNeg = lat < 0;
    bool lonNeg = lon < 0;
    double absLat = qAbs(lat);
    double absLon = qAbs(lon);

    int latDeg = (int)absLat;
    double latMinD = (absLat - latDeg) * 60.0;
    int latMin = (int)latMinD;
    int latMinFrac = (int)qRound((latMinD - latMin) * 100.0);
    if (latMinFrac >= 100) { latMinFrac = 0; latMin++; }
    if (latMin >= 60)      { latMin = 0; latDeg++; }

    int lonDeg = (int)absLon;
    double lonMinD = (absLon - lonDeg) * 60.0;
    int lonMin = (int)lonMinD;
    int lonMinFrac = (int)qRound((lonMinD - lonMin) * 100.0);
    if (lonMinFrac >= 100) { lonMinFrac = 0; lonMin++; }
    if (lonMin >= 60)      { lonMin = 0; lonDeg++; }

    QString out = QStringLiteral("!%1%2.%3%4%5%6%7.%8%9%10")
        .arg(latDeg, 2, 10, QChar('0'))
        .arg(latMin, 2, 10, QChar('0'))
        .arg(latMinFrac, 2, 10, QChar('0'))
        .arg(latNeg ? 'S' : 'N')
        .arg(symTable.isNull() ? QChar('/') : symTable)
        .arg(lonDeg, 3, 10, QChar('0'))
        .arg(lonMin, 2, 10, QChar('0'))
        .arg(lonMinFrac, 2, 10, QChar('0'))
        .arg(lonNeg ? 'W' : 'E')
        .arg(symCode.isNull() ? QChar('.') : symCode);

    QString c = comment;
    if (c.size() > 43) c = c.left(43);
    out += c;
    return out.toLatin1();
}

// ---------------------------------------------------------------------------
// RX hook
// ---------------------------------------------------------------------------

void AprsProcessor::onRxFrame(int chan, QJsonObject frame)
{
    Q_UNUSED(chan);
    if (frame.value("ftype").toString() != "UI") return;

    QString src  = frame.value("src").toString().trimmed();
    QString dst  = frame.value("dst").toString().trimmed();
    QString info = frame.value("info").toString();
    if (src.isEmpty() || info.isEmpty()) return;

    double lat = 0, lon = 0;
    QChar  st = '/', sc = '.';
    QString comment;
    if (!parsePosition(dst, info, lat, lon, st, sc, comment)) return;

    Station &st_entry = stations_[src];
    st_entry.src      = src;
    st_entry.lat      = lat;
    st_entry.lon      = lon;
    st_entry.symTable = st;
    st_entry.symCode  = sc;
    st_entry.comment  = comment;
    QStringList path;
    QJsonArray pa = frame.value("path").toArray();
    for (const QJsonValue &v : pa) path.append(v.toString());
    st_entry.path = path;
    st_entry.lastHeardMs = QDateTime::currentMSecsSinceEpoch();
    st_entry.count++;

    QJsonObject out = stationToJson(st_entry);
    out["type"] = "aprsStation";
    emit stationUpdated(out);
}

// ---------------------------------------------------------------------------
// Beacon
// ---------------------------------------------------------------------------

void AprsProcessor::setBeaconConfig(bool enabled, int intervalSec,
                                    const QString &src,
                                    double lat, double lon,
                                    QChar symTable, QChar symCode,
                                    const QString &comment,
                                    const QStringList &path)
{
    beaconEnabled_ = enabled;
    if (intervalSec > 0) beaconIntervalSec_ = intervalSec;
    beaconSrc_      = src;
    beaconLat_      = lat;
    beaconLon_      = lon;
    beaconSymTable_ = symTable.isNull() ? QChar('/') : symTable;
    beaconSymCode_  = symCode.isNull()  ? QChar('.') : symCode;
    beaconComment_  = comment;
    beaconPath_     = path;

    if (beaconEnabled_ && !beaconSrc_.isEmpty() && beaconIntervalSec_ > 0) {
        beaconTimer_->start(beaconIntervalSec_ * 1000);
    } else {
        beaconTimer_->stop();
    }
}

void AprsProcessor::txBeaconNow(const QString &src,
                                double lat, double lon,
                                QChar symTable, QChar symCode,
                                const QString &comment,
                                const QStringList &path)
{
    if (src.isEmpty()) return;
    QByteArray info = buildPositionInfo(lat, lon, symTable, symCode, comment);
    emit txBeaconRequested(src,
                           QStringLiteral("APWFWB"),
                           path,
                           QString::fromLatin1(info));
}

void AprsProcessor::onBeaconTimer()
{
    if (!beaconEnabled_ || beaconSrc_.isEmpty()) return;
    txBeaconNow(beaconSrc_, beaconLat_, beaconLon_,
                beaconSymTable_, beaconSymCode_,
                beaconComment_, beaconPath_);
}
