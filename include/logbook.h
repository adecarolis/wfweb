#ifndef LOGBOOK_H
#define LOGBOOK_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

// One QSO as the server stores it.  JSON only at the edges (REST/WebSocket);
// ADIF on disk.  Kept as a plain struct rather than a QJsonObject so a
// lifetime log (100k+ records) stays small in memory.
struct QsoRecord {
    QString id;         // stable UUID, persisted as APP_WFWEB_ID
    QString date;       // YYYYMMDD (UTC)
    QString time;       // HHMMSS (UTC)
    QString call;
    qint64  freq = 0;   // Hz
    QString band;
    QString mode;
    QString grid;       // MY_GRIDSQUARE
    QString theirGrid;  // GRIDSQUARE
    QString rstSent;
    QString rstRcvd;
    QString comment;
    QString name;
    QString stationCall;  // STATION_CALLSIGN: the call this station used for the QSO
    // Shared Station (--shared): who made the QSO.  operatorCall is ADIF's
    // OPERATOR, the person's own call; userId is the account that logged it,
    // persisted as APP_WFWEB_USER, and is what decides who may change the
    // record.  Both empty on a record logged without accounts or imported.
    QString operatorCall;
    QString userId;
    int     df = -1;    // digi audio offset in Hz, <0 = not set
    // UTC stamp (yyyyMMddTHHmmssZ) of the export that included this record,
    // persisted as APP_WFWEB_EXPORTED.  Empty = not exported yet ("new").
    // Set by the client that saved the export (see Logbook::markExported),
    // so a record logged while a download was in flight is never skipped.
    QString exported;

    bool isValid() const { return !call.isEmpty(); }
    // Chronological sort key.  Entries without date/time sort first (oldest).
    QString sortKey() const { return date + time; }
    QJsonObject toJson() const;
    // One <...> record terminated by <EOR>\n.  appFields=false leaves out the
    // APP_WFWEB_* bookkeeping (id, export stamp, digi offset, account) for
    // files meant for other logging services; OPERATOR is standard and stays.
    QByteArray toAdif(bool appFields = true) const;
    // Whitelists and length-caps untrusted input; empty call => invalid record.
    // Never reads userId: the account behind a record is the server's to say.
    static QsoRecord fromJson(const QJsonObject &in, const QString &id = QString());
    // Trim / upper-case / cap every field and mint an id; the single
    // normalisation path for JSON input and ADIF parsing alike.
    QsoRecord normalized() const;
};

// The station logbook: a plain ADIF file on disk plus a chronological
// in-memory index.  Adds append one record; edits and deletes rewrite the
// file atomically.  Never hands out the whole log — callers page through it.
class Logbook {
public:
    // Which records a caller is shown or may export (Shared Station); an
    // empty filter is every record.
    using Filter = std::function<bool(const QsoRecord &)>;
    // Record counts per (account, station call), kept as records change so
    // "how many does this viewer see" is a walk over a handful of pairs and
    // never a scan of the log per socket per QSO.
    struct Tally {
        int count = 0;
        int unexported = 0;
    };
    // The sum over the pairs `pick(userId, stationCall)` accepts.
    Tally tally(const std::function<bool(const QString &, const QString &)> &pick) const;

    struct Page {
        QList<QsoRecord> entries;   // newest first
        QString next;               // cursor for the following page, empty at the end
    };

    static QByteArray adifHeader();
    static QList<QsoRecord> parseAdif(const QByteArray &data);
    static QString exportStamp();   // now, in the APP_WFWEB_EXPORTED format
    // Does a logbook in `dir` outlive the process's container?
    //
    // Why this exists: before the server-side logbook, the QSO log lived in
    // each browser, so running wfweb in a throw-away container was harmless.
    // Now every browser hands its log to the server on first connect, and a
    // container started without a volume keeps that file only until it is
    // recreated (`docker run --rm`, an image upgrade, `docker compose up`).
    // Operators upgrading without reading DOCKER.md would lose their log
    // silently.  The server therefore checks, warns, and tells the browsers,
    // which keep their own copy while this is false (see the SPA's
    // logbookPersistent).
    //
    // Answers false only inside a container (Docker or Podman markers, or
    // the cgroup names) AND when `dir` is on the container's own root
    // filesystem or on an anonymous volume; see logbook.cpp for the exact
    // rules and their limits.  Never false outside a container.  Linux only;
    // on other platforms it is always true.
    static bool isPersistentLocation(const QString &dir);
    // Testable core of the above: `mountinfo` is the text of
    // /proc/self/mountinfo and `inContainer` the result of the marker check.
    static bool isPersistentLocation(const QString &dir, const QString &mountinfo, bool inContainer);
    static QString cursorFor(const QsoRecord &r) { return r.sortKey() + QLatin1Char('|') + r.id; }

    // Sets the path, creates the file with a header if missing, otherwise
    // loads it.  Returns false if the file could not be read or created.
    bool open(const QString &path);
    const QString &path() const { return path_; }
    int count() const { return records_.size(); }

    bool add(QsoRecord &r);                       // assigns r.id if empty
    bool update(const QString &id, QsoRecord r);  // r.id is replaced by id
    bool remove(const QString &id);
    // Never discards data: a non-empty file is renamed to
    // <path>.<yyyyMMdd-HHmmss>.bak before the header-only file is written.
    bool clear(QString *backupPath = nullptr);
    // Adds entries not already present (same date, time, call, freq, mode).
    // Returns the number added; one file rewrite for the whole batch.
    int merge(const QList<QsoRecord> &incoming);
    const QsoRecord *find(const QString &id) const;

    // Export bookkeeping.  toAdif(true) is the "download new" document;
    // markExported() stamps the given ids (one rewrite) and returns how many
    // records changed.
    int unexportedCount() const;
    QByteArray toAdif(bool unexportedOnly, bool appFields = true, const Filter &filter = Filter()) const;
    QStringList unexportedIds(const Filter &filter = Filter()) const;
    int markExported(const QStringList &ids, const Filter &filter = Filter());

    // Up to `limit` entries older than `before` (a cursor from a previous
    // page, empty = start from the newest), optionally only those with the
    // given callsign.
    Page page(int limit, const QString &before, const QString &call, const Filter &filter = Filter()) const;
    // Every distinct callsign with the bands it was worked on, as
    // {"K1ABC": ["20M", "40M"], ...}.  One pass over the log; the browser
    // derives its "new one" / "new on this band" hints from this rather than
    // paging through 100k records.
    QJsonObject workedCalls(const Filter &filter = Filter(), int *considered = nullptr) const;

private:
    int insertSorted(const QsoRecord &r);
    int indexOf(const QString &id) const;
    int lowerBound(const QString &sortKey, const QString &id) const;
    bool rewrite() const;
    bool append(const QsoRecord &r) const;
    void tallyRecord(const QsoRecord &r, int sign);
    void rebuildTally();

    QString path_;
    QList<QsoRecord> records_;   // ascending by (sortKey, id)
    QHash<QString, Tally> tally_;   // by userId + '\n' + stationCall
};

#endif // LOGBOOK_H
