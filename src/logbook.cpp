#include "logbook.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>

// ---------------------------------------------------------------------------
// QsoRecord
// ---------------------------------------------------------------------------

QsoRecord QsoRecord::normalized() const
{
    QsoRecord r;
    r.call = call.trimmed().toUpper();
    if (r.call.isEmpty() || r.call.size() > 32)
        return QsoRecord();   // invalid

    r.id        = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    r.date      = date.trimmed().left(8);
    r.time      = time.trimmed().left(6);
    r.band      = band.trimmed().left(16);
    r.mode      = mode.trimmed().left(16).toUpper();
    r.grid      = grid.trimmed().left(16).toUpper();
    r.theirGrid = theirGrid.trimmed().left(16).toUpper();
    r.rstSent   = rstSent.trimmed().left(16);
    r.rstRcvd   = rstRcvd.trimmed().left(16);
    r.comment   = comment.trimmed().left(256);
    r.name      = name.trimmed().left(128);
    r.stationCall = stationCall.trimmed().left(32).toUpper();
    r.operatorCall = operatorCall.trimmed().left(32).toUpper();
    r.userId    = userId.trimmed().left(64);
    r.freq      = freq > 0 ? freq : 0;
    r.df        = df;
    r.exported  = exported.trimmed().left(20);
    return r;
}

QsoRecord QsoRecord::fromJson(const QJsonObject &in, const QString &id)
{
    QsoRecord r;
    r.id        = id;
    r.call      = in.value("call").toString();
    r.date      = in.value("date").toString();
    r.time      = in.value("time").toString();
    r.band      = in.value("band").toString();
    r.mode      = in.value("mode").toString();
    r.grid      = in.value("grid").toString();
    r.theirGrid = in.value("theirGrid").toString();
    r.rstSent   = in.value("rstSent").toString();
    r.rstRcvd   = in.value("rstRcvd").toString();
    r.comment   = in.value("comment").toString();
    r.name      = in.value("name").toString();
    r.stationCall = in.value("stationCall").toString();
    r.operatorCall = in.value("operator").toString();
    r.freq      = in.value("freq").toVariant().toLongLong();
    r.exported  = in.value("exported").toString();   // round-trips through edits
    if (in.value("df").isDouble())
        r.df = in.value("df").toInt();
    return r.normalized();
}

QJsonObject QsoRecord::toJson() const
{
    QJsonObject o;
    o["id"]   = id;
    o["call"] = call;
    o["freq"] = freq;
    const auto put = [&o](const char *key, const QString &value) {
        if (!value.isEmpty()) o[QLatin1String(key)] = value;
    };
    put("date", date);
    put("time", time);
    put("band", band);
    put("mode", mode);
    put("grid", grid);
    put("theirGrid", theirGrid);
    put("rstSent", rstSent);
    put("rstRcvd", rstRcvd);
    put("comment", comment);
    put("name", name);
    put("stationCall", stationCall);
    put("operator", operatorCall);
    put("userId", userId);
    put("exported", exported);
    if (df >= 0) o["df"] = df;
    return o;
}

QByteArray QsoRecord::toAdif(bool appFields) const
{
    QByteArray out;
    // ADIF lengths are in bytes, so measure the UTF-8 encoding, not QChars.
    const auto field = [&out](const char *tag, const QString &value) {
        if (value.isEmpty()) return;
        const QByteArray bytes = value.toUtf8();
        out += '<';
        out += tag;
        out += ':';
        out += QByteArray::number(bytes.size());
        out += '>';
        out += bytes;
        out += ' ';
    };
    if (appFields) field("APP_WFWEB_ID", id);
    field("CALL", call);
    field("QSO_DATE", date);
    field("TIME_ON", time);
    if (freq > 0)
        field("FREQ", QString::number(freq / 1e6, 'f', 6));
    field("BAND", band);
    field("MODE", mode);
    field("RST_SENT", rstSent);
    field("RST_RCVD", rstRcvd);
    field("GRIDSQUARE", theirGrid);
    field("MY_GRIDSQUARE", grid);
    field("STATION_CALLSIGN", stationCall);
    field("OPERATOR", operatorCall);
    field("COMMENT", comment);
    field("NAME", name);
    if (appFields) {
        if (df >= 0) field("APP_WFWEB_DF", QString::number(df));
        field("APP_WFWEB_EXPORTED", exported);
        field("APP_WFWEB_USER", userId);
    }
    out += "<EOR>\n";
    return out;
}

// ---------------------------------------------------------------------------
// Logbook — static helpers
// ---------------------------------------------------------------------------

QByteArray Logbook::adifHeader()
{
    return QByteArray("ADIF Export from wfweb\n"
                      "<ADIF_VER:5>3.1.4 <PROGRAMID:5>wfweb <EOH>\n");
}

QString Logbook::exportStamp()
{
    return QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
}

// Linear byte scanner: <NAME:len[:type]>value ... <EOR>.  Lengths are byte
// counts, tags are case-insensitive, anything outside <> is ignored, and
// whatever precedes <EOH> is discarded.  No regex and no QString copy of the
// whole file, so a 20 MB lifetime log loads in linear time.
QList<QsoRecord> Logbook::parseAdif(const QByteArray &data)
{
    QList<QsoRecord> out;
    const int n = data.size();
    int pos = 0;

    QHash<QByteArray, QByteArray> fields;
    const auto flush = [&fields, &out]() {
        if (fields.contains("CALL")) {
            QsoRecord r;
            r.id        = QString::fromUtf8(fields.value("APP_WFWEB_ID"));
            r.call      = QString::fromUtf8(fields.value("CALL"));
            r.date      = QString::fromUtf8(fields.value("QSO_DATE"));
            r.time      = QString::fromUtf8(fields.value("TIME_ON"));
            r.band      = QString::fromUtf8(fields.value("BAND"));
            r.mode      = QString::fromUtf8(fields.value("MODE"));
            r.rstSent   = QString::fromUtf8(fields.value("RST_SENT"));
            r.rstRcvd   = QString::fromUtf8(fields.value("RST_RCVD"));
            r.theirGrid = QString::fromUtf8(fields.value("GRIDSQUARE"));
            r.grid      = QString::fromUtf8(fields.value("MY_GRIDSQUARE"));
            r.comment   = QString::fromUtf8(fields.value("COMMENT"));
            r.name      = QString::fromUtf8(fields.value("NAME"));
            r.stationCall = QString::fromUtf8(fields.value("STATION_CALLSIGN"));
            r.operatorCall = QString::fromUtf8(fields.value("OPERATOR"));
            r.userId    = QString::fromUtf8(fields.value("APP_WFWEB_USER"));
            r.freq      = qRound64(fields.value("FREQ").toDouble() * 1e6);
            r.exported  = QString::fromUtf8(fields.value("APP_WFWEB_EXPORTED"));
            if (fields.contains("APP_WFWEB_DF"))
                r.df = fields.value("APP_WFWEB_DF").toInt();
            r = r.normalized();
            if (r.isValid()) out.append(r);
        }
        fields.clear();
    };

    while (pos < n) {
        const int lt = data.indexOf('<', pos);
        if (lt < 0) break;
        const int gt = data.indexOf('>', lt);
        if (gt < 0) break;
        const QByteArray tag = data.mid(lt + 1, gt - lt - 1);
        pos = gt + 1;

        if (tag.compare("EOR", Qt::CaseInsensitive) == 0) { flush(); continue; }
        if (tag.compare("EOH", Qt::CaseInsensitive) == 0) { fields.clear(); continue; }
        const int c1 = tag.indexOf(':');
        if (c1 < 0) continue;   // <EOH>, stray markup
        const int c2 = tag.indexOf(':', c1 + 1);
        int len = tag.mid(c1 + 1, c2 < 0 ? -1 : c2 - c1 - 1).toInt();
        if (len < 0) len = 0;
        if (pos + len > n) len = n - pos;
        fields[tag.left(c1).toUpper()] = data.mid(pos, len);
        pos += len;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Logbook — file
// ---------------------------------------------------------------------------

bool Logbook::open(const QString &path)
{
    path_ = path;
    records_.clear();
    tally_.clear();
    QDir().mkpath(QFileInfo(path_).absolutePath());

    QFile file(path_);
    if (!file.exists())
        return rewrite();   // header only
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = file.readAll();
    file.close();
    records_ = parseAdif(data);
    std::sort(records_.begin(), records_.end(), [](const QsoRecord &a, const QsoRecord &b) {
        const QString ka = a.sortKey(), kb = b.sortKey();
        return ka != kb ? ka < kb : a.id < b.id;
    });
    rebuildTally();

    // Records that came from another program have no APP_WFWEB_ID; fromJson
    // minted one, so persist it now or the ids change on every restart.
    // (Only true for freshly imported files — a normal reload rewrites nothing.)
    if (!records_.isEmpty() && !data.contains("APP_WFWEB_ID"))
        return rewrite();
    return true;
}

bool Logbook::rewrite() const
{
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(adifHeader());
    for (const QsoRecord &r : records_)
        file.write(r.toAdif());
    return file.commit();
}

bool Logbook::append(const QsoRecord &r) const
{
    if (!QFile::exists(path_))
        return rewrite();   // file was removed underneath us: rebuild with header
    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    const QByteArray rec = r.toAdif();
    return file.write(rec) == rec.size();
}

// ---------------------------------------------------------------------------
// Logbook — index
// ---------------------------------------------------------------------------

int Logbook::lowerBound(const QString &sortKey, const QString &id) const
{
    auto it = std::lower_bound(records_.cbegin(), records_.cend(), std::make_pair(sortKey, id),
        [](const QsoRecord &r, const std::pair<QString, QString> &key) {
            const QString k = r.sortKey();
            return k != key.first ? k < key.first : r.id < key.second;
        });
    return int(it - records_.cbegin());
}

int Logbook::insertSorted(const QsoRecord &r)
{
    const int idx = lowerBound(r.sortKey(), r.id);
    records_.insert(idx, r);
    return idx;
}

int Logbook::indexOf(const QString &id) const
{
    for (int i = 0; i < records_.size(); ++i)
        if (records_[i].id == id) return i;
    return -1;
}

const QsoRecord *Logbook::find(const QString &id) const
{
    const int idx = indexOf(id);
    return idx < 0 ? nullptr : &records_[idx];
}

bool Logbook::add(QsoRecord &r)
{
    if (!r.isValid()) return false;
    if (r.id.isEmpty()) r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int idx = insertSorted(r);
    if (append(r)) { tallyRecord(r, +1); return true; }
    records_.removeAt(idx);
    return false;
}

bool Logbook::update(const QString &id, QsoRecord r)
{
    const int idx = indexOf(id);
    if (idx < 0 || !r.isValid()) return false;
    const QList<QsoRecord> backup = records_;   // implicitly shared, cheap
    const QsoRecord old = records_[idx];
    records_.removeAt(idx);
    r.id = id;
    insertSorted(r);
    if (rewrite()) { tallyRecord(old, -1); tallyRecord(r, +1); return true; }
    records_ = backup;
    return false;
}

bool Logbook::remove(const QString &id)
{
    const int idx = indexOf(id);
    if (idx < 0) return false;
    const QList<QsoRecord> backup = records_;
    const QsoRecord old = records_[idx];
    records_.removeAt(idx);
    if (rewrite()) { tallyRecord(old, -1); return true; }
    records_ = backup;
    return false;
}

bool Logbook::clear(QString *backupPath)
{
    if (backupPath) backupPath->clear();
    if (!records_.isEmpty() && QFile::exists(path_)) {
        const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss");
        QString bak = path_ + '.' + stamp + ".bak";
        for (int n = 2; QFile::exists(bak); ++n)   // two clears in one second
            bak = path_ + '.' + stamp + '-' + QString::number(n) + ".bak";
        if (!QFile::rename(path_, bak)) return false;
        if (backupPath) *backupPath = bak;
    }
    const QList<QsoRecord> backup = records_;
    records_.clear();
    if (rewrite()) { tally_.clear(); return true; }
    records_ = backup;
    return false;
}

// ---------------------------------------------------------------------------
// Persistence check (containers)
//
// Why: the logbook used to live in each browser, so an ephemeral container
// lost nothing.  With the server owning the log, a browser hands its copy
// over on first connect and a container started without a volume keeps that
// file only until it is recreated (`docker run --rm`, an image upgrade,
// `docker compose up`).  Nobody wants to discover that after the fact, so
// the server works out whether its logbook directory will survive and, if
// not, warns at startup and tells every browser, which then keeps its own
// copy of what it logs and re-sends it on connect (SPA: logbookPersistent).
//
// How, in two steps:
//   1. "Am I in a container?"  Docker creates /.dockerenv in every
//      container and Podman creates /run/.containerenv.  As a fallback
//      /proc/1/cgroup is scanned for the runtime names; on cgroup-v2 hosts
//      that file often reads just "0::/" inside a container, so the marker
//      files are the reliable signal.  Outside a container the answer is
//      always "persistent": this check never produces a false alarm on a
//      bare-metal or VM install.
//   2. "Is the logbook directory on something that outlives the
//      container?"  /proc/self/mountinfo lists every mount visible to the
//      process.  Take the deepest mount point that contains the directory:
//        - "/" is the container's own overlay filesystem: the file is part
//          of the container and is deleted with it.               -> false
//        - anything else is a named volume or a bind mount.        -> true
//        - except a Docker/Podman *anonymous* volume, recognisable by its
//          source path ".../volumes/<64 hex chars>/_data": it survives a
//          stop/start but `docker run --rm` deletes it on exit and a
//          recreate orphans it, so it is ephemeral in practice.   -> false
//      (A tmpfs mounted on the directory would pass as persistent; an
//      operator who does that chose it.)
//
// Limits: runtimes that leave no marker (Kubernetes with containerd or
// CRI-O, say) are not detected and get no warning.  A miss costs only the
// warning: the browser still drops its old copy only after the server has
// confirmed the merge (logbookMerged), never on a fire-and-forget send.
// This is also why docker/Dockerfile has no VOLUME instruction: it would
// give an unmounted /data a hidden anonymous volume that masks the problem.
// ---------------------------------------------------------------------------

bool Logbook::isPersistentLocation(const QString &dir, const QString &mountinfo, bool inContainer)
{
    if (!inContainer) return true;

    // mountinfo line: "ID PARENT MAJ:MIN ROOT MOUNTPOINT OPTIONS ... - FSTYPE SOURCE SUPEROPTS".
    // ROOT (f[3]) is the path inside the source filesystem, which for a
    // Docker volume is /var/lib/docker/volumes/<name>/_data; MOUNTPOINT
    // (f[4]) is where it appears in the container, with spaces as \040.
    const QString target = QDir::cleanPath(dir);
    QString best, bestRoot;
    const QStringList lines = mountinfo.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList f = line.split(' ');
        if (f.size() < 5) continue;
        QString mp = f[4];
        mp.replace("\\040", " ");
        // Path-boundary match: "/data" contains "/data/x" but not "/database".
        const bool contains = mp == "/" || target == mp || target.startsWith(mp + '/');
        if (contains && mp.size() > best.size()) { best = mp; bestRoot = f[3]; }
    }
    if (best.isEmpty() || best == "/") return false;   // on the container's own overlay
    static const QRegularExpression anonymousVolume("/volumes/[0-9a-f]{64}/_data$");
    return !anonymousVolume.match(bestRoot).hasMatch();   // named volume / bind mount: yes
}

bool Logbook::isPersistentLocation(const QString &dir)
{
#ifdef Q_OS_LINUX
    // Step 1: container markers (see the block comment above).
    bool inContainer = QFile::exists("/.dockerenv") || QFile::exists("/run/.containerenv");
    if (!inContainer) {
        QFile cg("/proc/1/cgroup");
        if (cg.open(QIODevice::ReadOnly)) {
            const QByteArray c = cg.readAll();
            inContainer = c.contains("docker") || c.contains("containerd") || c.contains("kubepods") || c.contains("libpod");
        }
    }
    if (!inContainer) return true;
    // Step 2: what is mounted under the logbook directory.
    QFile mi("/proc/self/mountinfo");
    if (!mi.open(QIODevice::ReadOnly)) return true;   // can't tell: don't cry wolf
    return isPersistentLocation(dir, QString::fromUtf8(mi.readAll()), true);
#else
    Q_UNUSED(dir)
    return true;   // containers are a Linux deployment story here
#endif
}

int Logbook::merge(const QList<QsoRecord> &incoming)
{
    const auto dupKey = [](const QsoRecord &r) {
        return r.date + '|' + r.time + '|' + r.call + '|' + QString::number(r.freq) + '|' + r.mode;
    };
    QSet<QString> seen;
    for (const QsoRecord &r : records_) seen.insert(dupKey(r));
    const QList<QsoRecord> backup = records_;

    int added = 0;
    for (QsoRecord r : incoming) {
        if (!r.isValid()) continue;
        const QString key = dupKey(r);
        if (seen.contains(key)) continue;
        seen.insert(key);
        if (r.id.isEmpty()) r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertSorted(r);
        ++added;
    }
    if (added && !rewrite()) {
        records_ = backup;   // never leave memory and disk disagreeing
        return 0;
    }
    if (added) rebuildTally();
    return added;
}

Logbook::Page Logbook::page(int limit, const QString &before, const QString &call, const Filter &filter) const
{
    Page result;
    if (limit <= 0) limit = 100;
    if (limit > 1000) limit = 1000;

    // Start just past the newest record, or at the cursor's own position so
    // the page holds strictly older entries.  A cursor whose record has since
    // been deleted still resolves to the right place via lower_bound.
    int end = records_.size();
    const int bar = before.indexOf('|');
    if (bar > 0)
        end = lowerBound(before.left(bar), before.mid(bar + 1));

    const QString wanted = call.trimmed().toUpper();
    int i = end;
    while (i > 0 && result.entries.size() < limit) {
        --i;
        if (!wanted.isEmpty() && records_[i].call != wanted) continue;
        if (filter && !filter(records_[i])) continue;
        result.entries.append(records_[i]);
    }
    // With a filter the rest of the log may hold nothing for this caller:
    // say "more" only when it does, or the page would end on an empty fetch.
    bool more = i > 0;
    if (more && filter) {
        more = false;
        for (int j = i - 1; j >= 0 && !more; --j)
            more = (wanted.isEmpty() || records_[j].call == wanted) && filter(records_[j]);
    }
    if (more && !result.entries.isEmpty())
        result.next = cursorFor(result.entries.last());
    return result;
}

// ---------------------------------------------------------------------------
// Export bookkeeping
// ---------------------------------------------------------------------------

int Logbook::unexportedCount() const
{
    int n = 0;
    for (const QsoRecord &r : records_)
        if (r.exported.isEmpty()) ++n;
    return n;
}

QByteArray Logbook::toAdif(bool unexportedOnly, bool appFields, const Filter &filter) const
{
    QByteArray out = adifHeader();
    for (const QsoRecord &r : records_)
        if ((!unexportedOnly || r.exported.isEmpty()) && (!filter || filter(r)))
            out += r.toAdif(appFields);
    return out;
}

QJsonObject Logbook::workedCalls(const Filter &filter, int *considered) const
{
    QHash<QString, QStringList> bands;
    int n = 0;
    for (const QsoRecord &r : records_) {
        if (filter && !filter(r)) continue;
        ++n;
        if (r.call.isEmpty()) continue;
        QStringList &list = bands[r.call];
        const QString band = r.band.isEmpty() ? QStringLiteral("?") : r.band;
        if (!list.contains(band)) list.append(band);
    }
    if (considered) *considered = n;
    QJsonObject out;
    for (auto it = bands.cbegin(); it != bands.cend(); ++it)
        out.insert(it.key(), QJsonArray::fromStringList(it.value()));
    return out;
}

QStringList Logbook::unexportedIds(const Filter &filter) const
{
    QStringList ids;
    for (const QsoRecord &r : records_)
        if (r.exported.isEmpty() && (!filter || filter(r))) ids.append(r.id);
    return ids;
}

int Logbook::markExported(const QStringList &ids, const Filter &filter)
{
    const QSet<QString> wanted(ids.cbegin(), ids.cend());
    const QString stamp = exportStamp();
    const QList<QsoRecord> backup = records_;
    int changed = 0;
    for (QsoRecord &r : records_) {
        if (r.exported.isEmpty() && wanted.contains(r.id) && (!filter || filter(r))) {
            r.exported = stamp;
            ++changed;
        }
    }
    if (changed && !rewrite()) {
        records_ = backup;
        return 0;
    }
    if (changed) rebuildTally();
    return changed;
}

// ---------------------------------------------------------------------------
// Counts per (account, station call)
// ---------------------------------------------------------------------------

void Logbook::tallyRecord(const QsoRecord &r, int sign)
{
    const QString key = r.userId + QLatin1Char('\n') + r.stationCall;
    Tally &t = tally_[key];
    t.count += sign;
    if (r.exported.isEmpty()) t.unexported += sign;
    if (t.count <= 0) tally_.remove(key);
}

void Logbook::rebuildTally()
{
    tally_.clear();
    for (const QsoRecord &r : records_) tallyRecord(r, +1);
}

Logbook::Tally Logbook::tally(const std::function<bool(const QString &, const QString &)> &pick) const
{
    Tally sum;
    for (auto it = tally_.cbegin(); it != tally_.cend(); ++it) {
        const int nl = it.key().indexOf(QLatin1Char('\n'));
        if (!pick(it.key().left(nl), it.key().mid(nl + 1))) continue;
        sum.count += it.value().count;
        sum.unexported += it.value().unexported;
    }
    return sum;
}
