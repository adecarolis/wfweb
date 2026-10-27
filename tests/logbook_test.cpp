// Unit test for the Logbook class (include/logbook.h): ADIF parsing, the
// chronological index, append-vs-rewrite on disk, paging cursors and merge
// dedupe.  Built and run by tests/test_cpp_units.py.
#include "logbook.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) { qCritical() << "FAIL:" << message; ::exit(1); }
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

QsoRecord qso(const char *call, const char *date, const char *time, qint64 freq = 14074000)
{
    QJsonObject o{{"call", call}, {"date", date}, {"time", time}, {"freq", freq}, {"mode", "FT8"}};
    return QsoRecord::fromJson(o);
}
}

int main()
{
    // --- parser: lower-case tags, CRLF, type suffix, UTF-8 byte lengths, junk ---
    {
        const QByteArray adif = QByteArray(
            "Header text <adif_ver:5>3.1.4 <programid:4>Test <eoh>\r\n"
            "<call:5>IK1ZZ <qso_date:8:D>20200505 <time_on:6>101010 <freq:8>7.074000 "
            "<name:5>Jos\xc3\xa9 <gridsquare:4>JN45 junk between fields <eor>\r\n"
            "<CALL:0> <QSO_DATE:8>20200506 <EOR>\n"                       // no call: dropped
            "<call:6>K1TEST <app_wfweb_id:3>abc <app_wfweb_df:4>1500 <station_callsign:5>w2erc <eor>");  // no trailing newline
        const QList<QsoRecord> recs = Logbook::parseAdif(adif);
        require(recs.size() == 2, "parser: two valid records expected");
        require(recs[0].call == "IK1ZZ" && recs[0].date == "20200505" && recs[0].time == "101010", "parser: core fields");
        require(recs[0].freq == 7074000, "parser: FREQ MHz -> Hz");
        require(recs[0].name == QString::fromUtf8("Jos\xc3\xa9"), "parser: UTF-8 value measured in bytes");
        require(recs[0].theirGrid == "JN45", "parser: GRIDSQUARE -> theirGrid");
        require(!recs[0].id.isEmpty(), "parser: missing APP_WFWEB_ID gets a fresh id");
        require(recs[1].id == "abc" && recs[1].df == 1500, "parser: APP_WFWEB_ID / APP_WFWEB_DF round-trip");
        require(recs[1].stationCall == "W2ERC" && recs[0].stationCall.isEmpty(), "parser: STATION_CALLSIGN read and upper-cased, absent stays empty");
    }

    // --- record round trip through toAdif/parseAdif ---
    {
        QsoRecord r = qso("K1TEST", "20260917", "120000");
        r.name = QString::fromUtf8("Zo\xc3\xab");
        r.comment = "portable";
        r.stationCall = "K1FM";   // records are normalised at the JSON/ADIF boundary, not on assignment
        r.df = 1234;
        const QList<QsoRecord> back = Logbook::parseAdif(Logbook::adifHeader() + r.toAdif());
        require(back.size() == 1, "round trip: one record");
        require(back[0].id == r.id && back[0].name == r.name && back[0].comment == r.comment
                && back[0].freq == r.freq && back[0].df == 1234, "round trip: fields identical");
        require(back[0].stationCall == "K1FM" && r.toAdif(false).contains("<STATION_CALLSIGN:4>K1FM"), "round trip: STATION_CALLSIGN is a standard field, kept in clean exports");
    }

    QTemporaryDir tmp;
    require(tmp.isValid(), "temp dir");
    const QString path = tmp.filePath("nested/dir/logbook.adi");

    // --- open creates the file (and directories) with a header ---
    Logbook lb;
    require(lb.open(path), "open: create");
    require(QFile::exists(path) && readAll(path).contains("<EOH>"), "open: header written");
    require(lb.count() == 0, "open: empty");

    // --- add appends (header stays, file grows by one record) ---
    QsoRecord a = qso("N2MID", "20260102", "120000");
    QsoRecord b = qso("N1OLD", "20260101", "090000");
    QsoRecord c = qso("N3NEW", "20260103", "180000");
    require(lb.add(a) && lb.add(b) && lb.add(c), "add: three records");
    require(!a.id.isEmpty() && a.id != b.id, "add: ids assigned");
    {
        const QByteArray disk = readAll(path);
        require(disk.count("<EOR>") == 3 && disk.count("<EOH>") == 1, "add: appended, single header");
        // append order == insertion order on disk; the index sorts on load
        require(disk.indexOf("N2MID") < disk.indexOf("N1OLD"), "add: append, not rewrite");
    }

    // --- page: newest first, cursor walks backwards, filter by call ---
    {
        Logbook::Page p = lb.page(2, QString(), QString());
        require(p.entries.size() == 2 && p.entries[0].call == "N3NEW" && p.entries[1].call == "N2MID", "page: newest first");
        require(!p.next.isEmpty(), "page: cursor when more remain");
        Logbook::Page p2 = lb.page(2, p.next, QString());
        require(p2.entries.size() == 1 && p2.entries[0].call == "N1OLD" && p2.next.isEmpty(), "page: last page, no cursor");
        Logbook::Page f = lb.page(10, QString(), "n1old");
        require(f.entries.size() == 1 && f.entries[0].call == "N1OLD", "page: call filter is case-insensitive");
        require(lb.page(10, QString(), "NOBODY").entries.isEmpty(), "page: filter with no match");
    }

    // --- update re-sorts and rewrites; delete rewrites ---
    {
        QsoRecord moved = qso("N1OLD", "20260104", "000000");   // now the newest
        require(lb.update(b.id, moved), "update");
        require(lb.find(b.id) && lb.find(b.id)->date == "20260104", "update: id kept, fields replaced");
        require(lb.page(1, QString(), QString()).entries[0].id == b.id, "update: re-sorted to the top");
        require(!lb.update("nope", moved), "update: unknown id");
        require(lb.remove(a.id), "remove");
        require(!lb.find(a.id) && lb.count() == 2, "remove: gone");
        require(!readAll(path).contains("N2MID"), "remove: rewritten on disk");
    }

    // --- reload from disk gives the same, sorted, index ---
    {
        Logbook again;
        require(again.open(path), "reload");
        require(again.count() == 2, "reload: count");
        const Logbook::Page p = again.page(10, QString(), QString());
        require(p.entries[0].id == b.id && p.entries[1].id == c.id, "reload: order and ids stable");
    }

    // --- merge dedupes on date/time/call/freq/mode ---
    {
        QList<QsoRecord> in;
        in << qso("N3NEW", "20260103", "180000")   // duplicate of c
           << qso("N9ABC", "20250101", "000000")
           << QsoRecord();                          // invalid, skipped
        require(lb.merge(in) == 1 && lb.count() == 3, "merge: one added");
        require(lb.merge(in) == 0, "merge: idempotent");
    }

    // --- a foreign file (no ids) gets ids persisted on first open ---
    {
        const QString foreign = tmp.filePath("foreign.adi");
        QFile f(foreign);
        f.open(QIODevice::WriteOnly);
        f.write("<eoh>\n<call:5>IK1ZZ <qso_date:8>20200505 <eor>\n<call:5>IK2ZZ <qso_date:8>20200506 <eor>\n");
        f.close();
        Logbook imported;
        require(imported.open(foreign), "foreign: open");
        require(imported.count() == 2, "foreign: two records");
        require(readAll(foreign).count("<APP_WFWEB_ID:") == 2, "foreign: ids written back once");
        const QString firstId = imported.page(10, QString(), QString()).entries[0].id;
        Logbook reopened;
        reopened.open(foreign);
        require(reopened.page(10, QString(), QString()).entries[0].id == firstId, "foreign: ids stable across restarts");
    }

    // --- export bookkeeping ---
    {
        require(lb.unexportedCount() == 3, "export: everything new to start with");
        require(Logbook::parseAdif(lb.toAdif(true)).size() == 3, "export: toAdif(true) lists the new ones");
        const QsoRecord newest = lb.page(1, QString(), QString()).entries[0];
        require(lb.markExported({newest.id, "nope"}) == 1, "export: mark one");
        require(lb.unexportedCount() == 2 && !lb.find(newest.id)->exported.isEmpty(), "export: stamped");
        require(lb.find(newest.id)->exported.size() == 16, "export: stamp format yyyyMMddTHHmmssZ");
        require(lb.markExported({newest.id}) == 0, "export: idempotent");
        require(Logbook::parseAdif(lb.toAdif(true)).size() == 2, "export: toAdif(true) excludes it");
        const QByteArray clean = lb.toAdif(true, false);
        require(!clean.contains("APP_WFWEB") && Logbook::parseAdif(clean).size() == 2, "export: appFields=false strips the bookkeeping");
        require(lb.unexportedIds().size() == 2 && !lb.unexportedIds().contains(newest.id), "export: unexportedIds");
        Logbook again;
        again.open(path);
        require(again.unexportedCount() == 2 && again.find(newest.id)->exported == lb.find(newest.id)->exported, "export: stamp persisted in APP_WFWEB_EXPORTED");
    }

    // --- clear never discards data: the old file becomes a .bak ---
    {
        const int before = lb.count();
        QString bak;
        require(lb.clear(&bak) && lb.count() == 0, "clear");
        require(readAll(path).count("<EOR>") == 0 && readAll(path).contains("<EOH>"), "clear: header only");
        require(!bak.isEmpty() && bak.startsWith(path) && bak.endsWith(".bak") && QFile::exists(bak), "clear: backup file next to the logbook");
        require(Logbook::parseAdif(readAll(bak)).size() == before, "clear: backup holds every record");
        QString none;
        require(lb.clear(&none) && none.isEmpty(), "clear: empty log makes no backup");
        QsoRecord again = qso("K1AGAIN", "20260918", "130000");
        QString bak2;
        require(lb.add(again) && lb.clear(&bak2) && bak2 != bak && QFile::exists(bak2), "clear: second backup in the same second gets a distinct name");
    }

    // --- persistence check (container without a volume) ---
    {
        const QString mi =
            "22 1 0:21 / / rw,relatime - overlay overlay rw,lowerdir=/x\n"
            "23 22 0:22 / /proc rw,nosuid - proc proc rw\n"
            "24 22 8:2 /var/lib/docker/volumes/v/_data /data rw,relatime - ext4 /dev/sda2 rw\n"
            "25 22 8:2 /srv/with\\040space /mnt/with\\040space rw - ext4 /dev/sda2 rw\n"
            "26 22 8:2 /var/lib/docker/volumes/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/_data /anon rw - ext4 /dev/sda2 rw\n";
        require(Logbook::isPersistentLocation("/data/wfweb/wfweb", mi, true), "persistent: under a mounted /data");
        require(Logbook::isPersistentLocation("/data", mi, true), "persistent: the mount point itself");
        require(!Logbook::isPersistentLocation("/root/.local/share/wfweb/wfweb", mi, true), "persistent: root overlay is not");
        require(!Logbook::isPersistentLocation("/database", mi, true), "persistent: prefix match respects path boundaries");
        require(Logbook::isPersistentLocation("/mnt/with space/log", mi, true), "persistent: octal-escaped mount point");
        require(!Logbook::isPersistentLocation("/anon/wfweb", mi, true), "persistent: Docker anonymous volume is ephemeral");
        require(Logbook::isPersistentLocation("/anything", mi, false), "persistent: not in a container => always true");
        require(!Logbook::isPersistentLocation("/anything", QString(), true), "persistent: empty mountinfo inside a container => not persistent");
    }

    qInfo() << "logbook_test: all checks passed";
    return 0;
}
