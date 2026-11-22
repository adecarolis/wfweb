#include "access.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <mutex>

// ---------------------------------------------------------------------------
// Roles
// ---------------------------------------------------------------------------

QString Access::roleName(Role role)
{
    switch (role) {
    case Role::Listen:  return QStringLiteral("listen");
    case Role::Control: return QStringLiteral("control");
    case Role::Operate: return QStringLiteral("operate");
    case Role::Admin:   return QStringLiteral("admin");
    case Role::None:    break;
    }
    return QStringLiteral("none");
}

Access::Role Access::roleFromName(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("listen"))  return Role::Listen;
    if (n == QLatin1String("control")) return Role::Control;
    if (n == QLatin1String("operate")) return Role::Operate;
    if (n == QLatin1String("admin"))   return Role::Admin;
    return Role::None;
}

// ---------------------------------------------------------------------------
// Capabilities: what each WebSocket command and REST route needs
// ---------------------------------------------------------------------------
// One table for the whole command set; webServer::handleCommand asks it
// before doing anything, and the page gets the list of what its role may not
// send so it can refuse locally.  Keep it complete: a command missing here
// is Admin-only (fail closed), and the unit test checks a few of each kind.

namespace {
const char *const kViewCommands[] = {
    "getStatus", "enableAudio", "getMemories", "termList", "termHistory", "aprsRequestSnapshot",
};
const char *const kControlCommands[] = {
    "setFrequency", "setBand", "setMode", "selectVFO", "swapVFO", "equalizeVFO",
    "setAfGain", "setRfGain", "setRfPower", "setSquelch", "setMicGain", "setAttenuator", "setPreamp",
    "setAntenna", "setNoiseBlanker", "setBreakIn", "setNoiseReduction", "setAGC", "setCompressor", "setTxBandwidth",
    "setCompLevel", "setMonitor", "setMonitorGain", "setTxMeter", "setAutoNotch", "setFilterWidth",
    "setPBTInner", "setPBTOuter", "setFilterShape", "setFilter", "setSplit", "setDuplex", "setToneMode",
    "setToneFreq", "setTsqlFreq", "setDtcsCode", "setDuplexOffset", "setSpan", "setScopeRef", "setScopeSpeed",
    "setScopeMode", "setScopeEdges", "setCWSpeed", "recallMemory", "setFreeDV", "setFreeDVTxGain",
    "setFreeDVMonitor", "packetEnable", "packetSetMode", "setDigiActive", "reportDigiSpot",
    "aprsClearStations",
    // The seat itself: asking for it, giving it up, answering a request.
    "seatTake", "seatRelease", "seatHandover", "seatCancel", "seatAccept", "seatPrivate",
};
const char *const kTxCommands[] = {
    "setPTT", "enableMic", "sendCW", "stopCW", "setTuner", "packetTx", "aprsTxBeacon",
    "termConnect", "termDisconnect", "termSend", "termClose", "termRegister", "termUnregister",
    "termFileSend", "termFileAbort", "termXferAccept", "termXferReject",
    // The log is what an operator writes after a QSO.
    "qsoLogged", "updateQso", "deleteQso",
};
const char *const kAdminCommands[] = {
    "setPower", "disconnectLan", "reconnectLan", "writeMemory", "renameMemory", "clearMemory",
    "setReporter", "setStationCallsign", "setCWMacros", "aprsBeaconConfig",
    "seatClear",
    // Whole-log operations: the migration of a browser's old local log.
    "mergeLogbook",
};
// Control / Tx commands that do not need the seat.
const char *const kSeatExempt[] = {
    "setDigiActive", "reportDigiSpot", "setTxMeter",
    // Correcting or removing a QSO one logged does not act on the radio.
    "updateQso", "deleteQso",
    "seatTake", "seatRelease", "seatHandover", "seatCancel", "seatAccept", "seatPrivate", "seatClear",
};

const QHash<QString, Access::Cap> &commandTable()
{
    static const QHash<QString, Access::Cap> table = [] {
        QHash<QString, Access::Cap> t;
        for (const char *c : kViewCommands) t.insert(QString::fromLatin1(c), Access::Cap::View);
        for (const char *c : kControlCommands) t.insert(QString::fromLatin1(c), Access::Cap::Control);
        for (const char *c : kTxCommands) t.insert(QString::fromLatin1(c), Access::Cap::Tx);
        for (const char *c : kAdminCommands) t.insert(QString::fromLatin1(c), Access::Cap::Admin);
        return t;
    }();
    return table;
}
} // namespace

Access::Role Access::capRole(Cap cap)
{
    switch (cap) {
    case Cap::View:    return Role::Listen;
    case Cap::Control: return Role::Control;
    case Cap::Tx:      return Role::Operate;
    case Cap::Admin:   break;
    }
    return Role::Admin;
}

bool Access::roleAllows(Role role, Cap cap)
{
    return role != Role::None && static_cast<int>(role) >= static_cast<int>(capRole(cap));
}

Access::Cap Access::commandCapability(const QString &cmd)
{
    return commandTable().value(cmd, Cap::Admin);
}

bool Access::restReadOnly(const QString &method, const QString &path)
{
    const QString m = method.toUpper();
    if (m == QLatin1String("GET") || m == QLatin1String("HEAD") || m == QLatin1String("OPTIONS"))
        return false;
    return path == QLatin1String("/api/v1/radio") || path.startsWith(QLatin1String("/api/v1/radio/"));
}

Access::Cap Access::restCapability(const QString &method, const QString &pathIn)
{
    const QString m = method.toUpper();
    if (m == QLatin1String("GET") || m == QLatin1String("HEAD") || m == QLatin1String("OPTIONS"))
        return Cap::View;
    // The web server hands the path over without its trailing slash; the
    // routes below are compared whole, so one that slipped through must not
    // read as a lesser route ("/api/v1/logbook/" is the whole log, not a QSO).
    QString path = pathIn;
    while (path.length() > 1 && path.endsWith(QLatin1Char('/'))) path.chop(1);
    // No row for /api/v1/radio: a shared station does not take writes to the
    // radio over REST at all (restReadOnly, asked first by the gate).
    if (path.startsWith(QLatin1String("/api/v1/logbook"))) {
        // Whole-log operations: clear, and the ADIF import.
        if ((m == QLatin1String("DELETE") && path == QLatin1String("/api/v1/logbook"))
            || path == QLatin1String("/api/v1/logbook/adif"))
            return Cap::Admin;
        return Cap::Tx;
    }
    if (path.startsWith(QLatin1String("/api/v1/sstv/images")))
        return Cap::Control;
    // /api/v1/station, /api/v1/cw/macros, /api/v1/shared/*, anything new
    return Cap::Admin;
}

bool Access::txInhibitExempt(const QString &cmd)
{
    static const QSet<QString> set{QStringLiteral("qsoLogged"), QStringLiteral("updateQso"),
                                   QStringLiteral("deleteQso"), QStringLiteral("stopCW")};
    return set.contains(cmd);
}

QStringList Access::commandsDeniedTo(Role role)
{
    QStringList out;
    const QHash<QString, Cap> &t = commandTable();
    for (auto it = t.constBegin(); it != t.constEnd(); ++it)
        if (!roleAllows(role, it.value())) out.append(it.key());
    out.sort();
    return out;
}

bool Access::seatExempt(const QString &cmd)
{
    for (const char *c : kSeatExempt)
        if (cmd == QLatin1String(c)) return true;
    return false;
}

QStringList Access::commandsNeedingSeat()
{
    QStringList out;
    const QHash<QString, Cap> &t = commandTable();
    for (auto it = t.constBegin(); it != t.constEnd(); ++it)
        if ((it.value() == Cap::Control || it.value() == Cap::Tx) && !seatExempt(it.key())) out.append(it.key());
    out.sort();
    return out;
}

// ---------------------------------------------------------------------------
// Records <-> JSON
// ---------------------------------------------------------------------------

static QString isoUtc(const QDateTime &t)
{
    return t.isValid() ? t.toUTC().toString(Qt::ISODate) : QString();
}

static QDateTime fromIso(const QJsonValue &v)
{
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODate);
    if (t.isValid()) t.setTimeSpec(Qt::UTC);
    return t;
}

QJsonObject AccessUser::toJson(bool withSecrets) const
{
    QJsonObject o{
        {"id", id}, {"name", name}, {"callsign", callsign},
        {"onAir", onAirOwn ? QStringLiteral("own") : QStringLiteral("station")},
        {"role", Access::roleName(role)}, {"disabled", disabled},
        {"created", isoUtc(created)}, {"lastLogin", isoUtc(lastLogin)},
        {"notes", notes},
    };
    if (withSecrets) o["passwordHash"] = passwordHash;
    QJsonArray toks;
    for (const AccessToken &t : tokens) {
        QJsonObject to{{"id", t.id}, {"name", t.name},
                       {"created", isoUtc(t.created)}, {"lastUsed", isoUtc(t.lastUsed)}};
        if (withSecrets) to["hash"] = QString::fromLatin1(t.hash);
        toks.append(to);
    }
    o["tokens"] = toks;
    return o;
}

AccessUser AccessUser::fromJson(const QJsonObject &o)
{
    AccessUser u;
    u.id = o["id"].toString();
    u.name = o["name"].toString();
    u.callsign = o["callsign"].toString();
    u.onAirOwn = o["onAir"].toString() == QLatin1String("own");
    u.role = Access::roleFromName(o["role"].toString());
    if (u.role == Access::Role::None) u.role = Access::Role::Listen;
    u.notes = o["notes"].toString();
    u.passwordHash = o["passwordHash"].toString();
    u.disabled = o["disabled"].toBool();
    u.created = fromIso(o["created"]);
    u.lastLogin = fromIso(o["lastLogin"]);
    const QJsonArray toks = o["tokens"].toArray();
    for (const QJsonValue &v : toks) {
        const QJsonObject to = v.toObject();
        AccessToken t;
        t.id = to["id"].toString();
        t.name = to["name"].toString();
        t.hash = to["hash"].toString().toLatin1();
        t.created = fromIso(to["created"]);
        t.lastUsed = fromIso(to["lastUsed"]);
        if (!t.id.isEmpty() && !t.hash.isEmpty()) u.tokens.append(t);
    }
    return u;
}

QJsonObject AccessPrincipal::toJson() const
{
    if (!authenticated) return QJsonObject{{"authenticated", false}};
    return QJsonObject{{"authenticated", true}, {"id", userId}, {"name", name},
                       {"callsign", callsign}, {"onAir", onAirOwn ? QStringLiteral("own") : QStringLiteral("station")},
                       {"role", Access::roleName(role)}, {"via", via}};
}

// ---------------------------------------------------------------------------
// PBKDF2: libcrypto when it can be found at runtime, Qt otherwise
// ---------------------------------------------------------------------------
// Qt loads libcrypto itself for TLS, so on every host that serves HTTPS the
// library is there; resolving two symbols from it costs no build dependency
// and makes a key derivation ten times faster than QPasswordDigestor.  Both
// compute the same function, so hashes are interchangeable (the unit test
// checks one against the other).

namespace {
typedef void EVP_MD_t;
typedef int (*Pbkdf2Fn)(const char *pass, int passlen, const unsigned char *salt, int saltlen,
                        int iter, const EVP_MD_t *digest, int keylen, unsigned char *out);
typedef const EVP_MD_t *(*Sha256Fn)(void);

Pbkdf2Fn nativePbkdf2 = nullptr;
Sha256Fn nativeSha256 = nullptr;

void resolveNativePbkdf2()
{
    static std::once_flag once;
    std::call_once(once, [] {
        struct Candidate { const char *name; const char *version; };
        static const Candidate candidates[] = {
#if defined(Q_OS_WIN)
            {"libcrypto-3-x64", nullptr}, {"libcrypto-3", nullptr},
            {"libcrypto-1_1-x64", nullptr}, {"libcrypto-1_1", nullptr},
#elif defined(Q_OS_MACOS)
            // Full paths only.  macOS answers a dlopen of the bare
            // "libcrypto.dylib" by aborting the process ("loading libcrypto
            // in an unsafe way"): the system's copy is a stub put there for
            // that purpose, so every --shared server died at start-up on a
            // Mac (CI, 2026-11-14).
            {"/opt/homebrew/opt/openssl@3/lib/libcrypto", "3"},
            {"/usr/local/opt/openssl@3/lib/libcrypto", "3"},
            {"/opt/homebrew/opt/openssl@1.1/lib/libcrypto", "1.1"},
            {"/usr/local/opt/openssl@1.1/lib/libcrypto", "1.1"},
#else
            {"crypto", "3"}, {"crypto", "1.1"}, {"crypto", nullptr},
#endif
        };
        for (const Candidate &c : candidates) {
            // Kept alive on purpose: QLibrary never unloads on destruction,
            // but a static makes the lifetime explicit.
            static QLibrary lib;
            lib.setFileNameAndVersion(QString::fromLatin1(c.name),
                                      c.version ? QString::fromLatin1(c.version) : QString());
            if (!lib.load()) continue;
            auto p = reinterpret_cast<Pbkdf2Fn>(lib.resolve("PKCS5_PBKDF2_HMAC"));
            auto s = reinterpret_cast<Sha256Fn>(lib.resolve("EVP_sha256"));
            if (p && s) { nativePbkdf2 = p; nativeSha256 = s; return; }
            lib.unload();
        }
    });
}

QByteArray pbkdf2Sha256(const QByteArray &password, const QByteArray &salt, int iterations, int keyLen)
{
    resolveNativePbkdf2();
    if (nativePbkdf2 && nativeSha256) {
        QByteArray out(keyLen, '\0');
        if (nativePbkdf2(password.constData(), password.size(),
                         reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
                         iterations, nativeSha256(), keyLen,
                         reinterpret_cast<unsigned char *>(out.data())) == 1)
            return out;
    }
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password, salt, iterations, keyLen);
}

bool constantTimeEqual(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); i++)
        diff |= static_cast<unsigned char>(a.at(i)) ^ static_cast<unsigned char>(b.at(i));
    return diff == 0;
}

const int kSaltBytes = 16;
const int kKeyBytes = 32;
const char kHashPrefix[] = "pbkdf2-sha256";
// Characters a setup code may use: no 0/O or 1/I to misread off a console.
const char kCodeAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
// lastSeen is touched at most this often, to keep sessions.json writes rare.
const qint64 kTouchIntervalMs = 5 * 60 * 1000;
} // namespace

bool AccessControl::nativePbkdf2Available()
{
    resolveNativePbkdf2();
    return nativePbkdf2 != nullptr;
}

int AccessControl::defaultIterations()
{
    return nativePbkdf2Available() ? 600000 : 150000;
}

QString AccessControl::hashPassword(const QString &password, int iterations)
{
    if (iterations < 1000) iterations = 1000;
    QByteArray salt(kSaltBytes, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), kSaltBytes / 4);
    const QByteArray key = pbkdf2Sha256(password.toUtf8(), salt, iterations, kKeyBytes);
    return QString("%1$%2$%3$%4").arg(QLatin1String(kHashPrefix)).arg(iterations)
            .arg(QString::fromLatin1(salt.toBase64()), QString::fromLatin1(key.toBase64()));
}

bool AccessControl::verifyPassword(const QString &password, const QString &hash)
{
    const QStringList parts = hash.split('$');
    if (parts.size() != 4 || parts[0] != QLatin1String(kHashPrefix)) return false;
    bool ok = false;
    const int iterations = parts[1].toInt(&ok);
    if (!ok || iterations < 1) return false;
    const QByteArray salt = QByteArray::fromBase64(parts[2].toLatin1());
    const QByteArray key = QByteArray::fromBase64(parts[3].toLatin1());
    if (salt.isEmpty() || key.isEmpty()) return false;
    const QByteArray derived = pbkdf2Sha256(password.toUtf8(), salt, iterations, key.size());
    return constantTimeEqual(derived, key);
}

// ---------------------------------------------------------------------------
// Tokens, ids, HTTP helpers
// ---------------------------------------------------------------------------

QByteArray AccessControl::randomToken()
{
    QByteArray raw(32, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), 8);
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray AccessControl::tokenHash(const QByteArray &token)
{
    return QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex();
}

QString AccessControl::randomId(int bytes)
{
    QByteArray raw((bytes + 3) / 4 * 4, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), raw.size() / 4);
    return QString::fromLatin1(raw.left(bytes).toHex());
}

QByteArray AccessControl::cookieValue(const QByteArray &cookieHeader, const QByteArray &name)
{
    const QList<QByteArray> parts = cookieHeader.split(';');
    for (const QByteArray &part : parts) {
        const QByteArray p = part.trimmed();
        if (p.size() > name.size() && p.startsWith(name) && p.at(name.size()) == '=')
            return p.mid(name.size() + 1).trimmed();
    }
    return QByteArray();
}

QByteArray AccessControl::bearerToken(const QByteArray &authorizationHeader)
{
    const QByteArray h = authorizationHeader.trimmed();
    if (h.size() < 8 || h.left(7).toLower() != "bearer ") return QByteArray();
    return h.mid(7).trimmed();
}

bool AccessControl::inSubnets(const QHostAddress &addrIn, const QList<QPair<QHostAddress, int>> &subnets)
{
    if (subnets.isEmpty()) return false;
    // A v4 peer on a dual-stack listener arrives as ::ffff:a.b.c.d, which
    // isInSubnet() would not place in a v4 block.
    bool v4 = false;
    const quint32 ip4 = addrIn.toIPv4Address(&v4);
    const QHostAddress addr = v4 ? QHostAddress(ip4) : addrIn;
    for (const auto &subnet : subnets)
        if (addr.isInSubnet(subnet.first, subnet.second)) return true;
    return false;
}

bool AccessControl::publicAddress(const QHostAddress &addrIn)
{
    bool v4 = false;
    const quint32 ip4 = addrIn.toIPv4Address(&v4);
    const QHostAddress addr = v4 ? QHostAddress(ip4) : addrIn;
    if (addr.isNull()) return false;
    static const QList<QPair<QHostAddress, int>> local = [] {
        QList<QPair<QHostAddress, int>> l;
        for (const char *net : {"0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8",
                                "169.254.0.0/16", "172.16.0.0/12", "192.168.0.0/16", "198.18.0.0/15",
                                "224.0.0.0/4", "240.0.0.0/4",
                                "::/128", "::1/128", "fe80::/10", "fec0::/10", "fc00::/7", "ff00::/8"})
            l.append(QHostAddress::parseSubnet(QLatin1String(net)));
        return l;
    }();
    if (addr.protocol() != QAbstractSocket::IPv4Protocol && addr.protocol() != QAbstractSocket::IPv6Protocol)
        return false;
    for (const auto &subnet : local)
        if (addr.isInSubnet(subnet.first, subnet.second)) return false;
    return true;
}

QHostAddress AccessControl::forwardedClient(const QHostAddress &peer, const QByteArray &forwardedFor,
                                            const QList<QPair<QHostAddress, int>> &trustedProxies)
{
    if (!inSubnets(peer, trustedProxies)) return peer;
    const QList<QByteArray> parts = forwardedFor.split(',');
    QHostAddress leftmost;
    for (int i = parts.size() - 1; i >= 0; --i) {
        QString item = QString::fromLatin1(parts.at(i).trimmed());
        // Some proxies write the port too: "[2001:db8::1]:443", "203.0.113.9:443".
        if (item.startsWith('[')) {
            const int end = item.indexOf(']');
            if (end > 0) item = item.mid(1, end - 1);
        } else if (item.count(':') == 1) {
            item = item.left(item.indexOf(':'));
        }
        QHostAddress addr(item);
        // Not an address ("unknown", nginx's "unix:"): written by a proxy
        // of ours that did not know its client, so nothing to its left is
        // vouched for.  It used to be skipped, which handed the next entry,
        // the client's own, back as the client.
        if (addr.isNull()) break;
        addr.setScopeId(QString());   // "fe80::1%any text" parses, zone and all
        if (!inSubnets(addr, trustedProxies)) return addr;
        leftmost = addr;    // a proxy's own address: keep walking left
    }
    // Every entry was a trusted proxy (a block drawn too wide): the furthest
    // one is the best guess.  Nothing parseable: the peer.
    return leftmost.isNull() ? peer : leftmost;
}

QString AccessControl::who(const QString &name, const QString &ip)
{
    return QString("%1@%2").arg(name.isEmpty() ? QStringLiteral("-") : name,
                                ip.isEmpty() ? QStringLiteral("-") : ip);
}

// ---------------------------------------------------------------------------
// AccessControl
// ---------------------------------------------------------------------------

AccessControl::AccessControl(const QString &dir)
    : dir_(dir), iterations_(defaultIterations())
{
    regenerateSetupCode();
}

QDateTime AccessControl::now() const
{
    return clock_ ? clock_() : QDateTime::currentDateTimeUtc();
}

bool AccessControl::load(QString *error)
{
    QDir d(dir_);
    if (!d.mkpath(QStringLiteral("."))) {
        if (error) *error = QString("cannot create %1").arg(dir_);
        return false;
    }
    // Only the service account reads these: hashes and session ids live here.
    QFile::setPermissions(dir_, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QJsonObject root;
    bool missing = false;
    users_.clear();
    if (!readJson(d.filePath(QStringLiteral("users.json")), &root, &missing)) {
        if (error) *error = QString("cannot read %1").arg(d.filePath(QStringLiteral("users.json")));
        return false;
    }
    if (!missing) {
        const QJsonArray arr = root["users"].toArray();
        for (const QJsonValue &v : arr) {
            AccessUser u = AccessUser::fromJson(v.toObject());
            if (!u.id.isEmpty() && !u.name.isEmpty()) users_.append(u);
        }
    }

    sessions_.clear();
    if (!readJson(d.filePath(QStringLiteral("sessions.json")), &root, &missing)) {
        // Nothing in it is worth refusing to start for: everyone logs in again.
        qWarning().noquote() << "Shared: cannot read" << d.filePath(QStringLiteral("sessions.json"))
                             << "- starting with no sessions, everyone logs in again";
        missing = true;
    }
    if (!missing) {
        const QJsonArray arr = root["sessions"].toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            AccessSession s;
            s.hash = o["hash"].toString().toLatin1();
            s.userId = o["user"].toString();
            s.created = fromIso(o["created"]);
            s.lastSeen = fromIso(o["lastSeen"]);
            s.ip = o["ip"].toString();
            s.userAgent = o["userAgent"].toString();
            // (No lastSeen: it would never expire.)
            if (!s.hash.isEmpty() && s.lastSeen.isValid() && findUser(s.userId) >= 0) sessions_.insert(s.hash, s);
        }
    }
    sessionsDirty_ = false;

    // A directory nobody can write would lose every login: prove it now.
    QFile probe(d.filePath(QStringLiteral(".write-test")));
    if (!probe.open(QIODevice::WriteOnly)) {
        if (error) *error = QString("%1 is not writable").arg(dir_);
        return false;
    }
    probe.close();
    probe.remove();
    return true;
}

bool AccessControl::readJson(const QString &path, QJsonObject *root, bool *missing) const
{
    *root = QJsonObject();
    *missing = false;
    QFile f(path);
    if (!f.exists()) { *missing = true; return true; }
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    *root = doc.object();
    return true;
}

bool AccessControl::writeJson(const QString &path, const QJsonObject &root) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    // Owner only, applied to the temporary file so the rename never exposes
    // a world-readable copy.
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool AccessControl::saveUsers()
{
    usersDirty_ = false;
    QJsonArray arr;
    for (const AccessUser &u : users_) arr.append(u.toJson(true));
    return writeJson(QDir(dir_).filePath(QStringLiteral("users.json")),
                     QJsonObject{{"version", 1}, {"users", arr}});
}

bool AccessControl::saveSessions()
{
    QJsonArray arr;
    for (const AccessSession &s : sessions_) {
        arr.append(QJsonObject{{"hash", QString::fromLatin1(s.hash)}, {"user", s.userId},
                               {"created", isoUtc(s.created)}, {"lastSeen", isoUtc(s.lastSeen)},
                               {"ip", s.ip}, {"userAgent", s.userAgent}});
    }
    sessionsDirty_ = false;
    return writeJson(QDir(dir_).filePath(QStringLiteral("sessions.json")),
                     QJsonObject{{"version", 1}, {"sessions", arr}});
}

// --- users -----------------------------------------------------------------

int AccessControl::findUser(const QString &id) const
{
    for (int i = 0; i < users_.size(); i++)
        if (users_.at(i).id == id) return i;
    return -1;
}

int AccessControl::findUserByName(const QString &name) const
{
    for (int i = 0; i < users_.size(); i++)
        if (users_.at(i).name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QList<AccessUser> AccessControl::users() const
{
    return users_;
}

AccessUser AccessControl::user(const QString &id, bool *found) const
{
    const int i = findUser(id);
    if (found) *found = i >= 0;
    return i >= 0 ? users_.at(i) : AccessUser();
}

AccessUser AccessControl::userByName(const QString &name, bool *found) const
{
    const int i = findUserByName(name);
    if (found) *found = i >= 0;
    return i >= 0 ? users_.at(i) : AccessUser();
}

bool AccessControl::hasAdmin() const
{
    for (const AccessUser &u : users_)
        if (u.role == Access::Role::Admin && !u.disabled) return true;
    return false;
}

bool AccessControl::validName(const QString &name, QString *error)
{
    static const QRegularExpression re(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9._-]{1,31}\\z"));
    if (!re.match(name).hasMatch()) {
        if (error) *error = QStringLiteral("user name: 2-32 letters, digits, . _ -");
        return false;
    }
    return true;
}

bool AccessControl::validPassword(const QString &password, QString *error)
{
    if (password.size() < 8) {
        if (error) *error = QStringLiteral("password: at least 8 characters");
        return false;
    }
    if (password.size() > 256) {
        if (error) *error = QStringLiteral("password: at most 256 characters");
        return false;
    }
    return true;
}

QString AccessControl::normalizeCallsign(const QString &callsign)
{
    return callsign.trimmed().toUpper().left(16);
}

QString AccessControl::addUser(const QString &name, const QString &passwordHash, Access::Role role,
                               const QString &callsign, QString *error)
{
    if (!validName(name, error)) return QString();
    if (findUserByName(name) >= 0) {
        if (error) *error = QStringLiteral("that user name is taken");
        return QString();
    }
    if (role == Access::Role::None) {
        if (error) *error = QStringLiteral("unknown role");
        return QString();
    }
    if (passwordHash.isEmpty()) {
        if (error) *error = QStringLiteral("no password");
        return QString();
    }
    // The callsign of an own-call account goes on the air (the RADE
    // end-of-over, the reporters, STATION_CALLSIGN): checked on every way in.
    if (!validCallsign(callsign)) {
        if (error) *error = QStringLiteral("callsign: letters, digits and '/' only");
        return QString();
    }
    AccessUser u;
    u.id = randomId();
    u.name = name;
    u.callsign = normalizeCallsign(callsign);
    u.role = role;
    u.passwordHash = passwordHash;
    u.created = now();
    users_.append(u);
    saveUsers();
    return u.id;
}

bool AccessControl::validPasswordHash(const QString &hash)
{
    const QStringList parts = hash.split('$');
    if (parts.size() != 4 || parts[0] != QLatin1String(kHashPrefix)) return false;
    bool ok = false;
    const int iterations = parts[1].toInt(&ok);
    // What hashPassword() writes, within sane bounds: a one-byte key in an
    // imported file let one password in several hundred through, and a
    // hundred million rounds is a login attempt that never ends.
    if (!ok || iterations < 1000 || iterations > 10 * 1000 * 1000) return false;
    return QByteArray::fromBase64(parts[2].toLatin1()).size() >= 8
        && QByteArray::fromBase64(parts[3].toLatin1()).size() == kKeyBytes;
}

QJsonObject AccessControl::exportUsers() const
{
    QJsonArray arr;
    for (const AccessUser &u : users_) {
        arr.append(QJsonObject{
            {"username", u.name}, {"callsign", u.callsign},
            {"role", Access::roleName(u.role)},
            {"onAir", u.onAirOwn ? QStringLiteral("own") : QStringLiteral("station")},
            {"notes", u.notes}, {"disabled", u.disabled},
            {"passwordHash", u.passwordHash},
        });
    }
    return QJsonObject{{"wfwebUsers", 1}, {"exported", isoUtc(now())}, {"users", arr}};
}

QString AccessControl::importUser(const QJsonObject &entry, const QString &passwordHash, QString *error)
{
    const QString name = entry.value("username").toString(entry.value("name").toString()).trimmed();
    const Access::Role role = Access::roleFromName(entry.value("role").toString());
    const QString callsign = entry.value("callsign").toString();
    if (role == Access::Role::None) {
        if (error) *error = QStringLiteral("role: listen, control, operate or admin");
        return QString();
    }
    if (!validCallsign(callsign)) {
        if (error) *error = QStringLiteral("callsign: letters, digits and '/' only");
        return QString();
    }
    if (!validPasswordHash(passwordHash)) {
        if (error) *error = QStringLiteral("no password");
        return QString();
    }
    const QString id = addUser(name, passwordHash, role, callsign, error);
    if (id.isEmpty()) return QString();
    const int i = findUser(id);
    users_[i].notes = normalizeNotes(entry.value("notes").toString());
    users_[i].onAirOwn = entry.value("onAir").toString() == QLatin1String("own") && !users_[i].callsign.isEmpty();
    users_[i].disabled = entry.value("disabled").toBool();
    saveUsers();
    return id;
}

bool AccessControl::setPasswordHash(const QString &id, const QString &passwordHash)
{
    const int i = findUser(id);
    if (i < 0 || passwordHash.isEmpty()) return false;
    users_[i].passwordHash = passwordHash;
    return saveUsers();
}

bool AccessControl::setRole(const QString &id, Access::Role role)
{
    const int i = findUser(id);
    if (i < 0 || role == Access::Role::None) return false;
    users_[i].role = role;
    return saveUsers();
}

bool AccessControl::setCallsign(const QString &id, const QString &callsign)
{
    const int i = findUser(id);
    if (i < 0) return false;
    users_[i].callsign = normalizeCallsign(callsign);
    return saveUsers();
}

QString AccessControl::normalizeNotes(const QString &notes)
{
    QString out;
    for (const QChar ch : notes)
        out += (ch.category() == QChar::Other_Control) ? QLatin1Char(' ') : ch;
    return out.simplified().left(kMaxNotes);
}

bool AccessControl::setNotes(const QString &id, const QString &notes)
{
    const int i = findUser(id);
    if (i < 0) return false;
    users_[i].notes = normalizeNotes(notes);
    return saveUsers();
}

bool AccessControl::validCallsign(const QString &callsign)
{
    const QString c = normalizeCallsign(callsign);
    if (c.isEmpty()) return true;
    bool digit = false, letter = false;
    for (const QChar ch : c) {
        if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) digit = true;
        else if (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) letter = true;
        else if (ch != QLatin1Char('/')) return false;
    }
    return digit && letter && c.size() >= 3 && !c.startsWith('/') && !c.endsWith('/');
}

bool AccessControl::setOnAir(const QString &id, const QString &onAir, QString *error)
{
    const int i = findUser(id);
    if (i < 0) {
        if (error) *error = QStringLiteral("no such user");
        return false;
    }
    const bool own = onAir == QLatin1String("own");
    if (!own && onAir != QLatin1String("station")) {
        if (error) *error = QStringLiteral("onAir: station or own");
        return false;
    }
    if (own && users_[i].callsign.isEmpty()) {
        if (error) *error = QStringLiteral("onAir own needs a callsign on the account");
        return false;
    }
    users_[i].onAirOwn = own;
    return saveUsers();
}

bool AccessControl::setDisabled(const QString &id, bool disabled)
{
    const int i = findUser(id);
    if (i < 0) return false;
    users_[i].disabled = disabled;
    if (disabled) dropSessionsOf(id);
    return saveUsers();
}

bool AccessControl::removeUser(const QString &id)
{
    const int i = findUser(id);
    if (i < 0) return false;
    dropSessionsOf(id);
    users_.removeAt(i);
    return saveUsers();
}

void AccessControl::dropSessionsOf(const QString &userId)
{
    bool changed = false;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->userId == userId) { it = sessions_.erase(it); changed = true; }
        else ++it;
    }
    if (changed) saveSessions();
}

// --- first run -------------------------------------------------------------

void AccessControl::regenerateSetupCode()
{
    QString code;
    for (int i = 0; i < 8; i++) {
        if (i == 4) code += '-';
        code += QLatin1Char(kCodeAlphabet[QRandomGenerator::system()->bounded(int(sizeof(kCodeAlphabet) - 1))]);
    }
    setupCode_ = code;
}

void AccessControl::enableRecovery()
{
    recovery_ = true;
    regenerateSetupCode();
    audit(QStringLiteral("recovery-enabled"), who(QString(), QString()), QStringLiteral("--shared-reset-admin"));
}

bool AccessControl::setupCodeMatches(const QString &code) const
{
    if (hasAdmin() && !recovery_) return false;
    QString given = code.toUpper();
    given.remove('-');
    given.remove(' ');
    QString want = setupCode_;
    want.remove('-');
    return !given.isEmpty() && given == want;
}

bool AccessControl::setupLocked(const QString &ip, int *retryAfterSecs) const
{
    return lockedOut(QStringLiteral("setup:") + ip, retryAfterSecs);
}

void AccessControl::setupFailed(const QString &ip)
{
    recordFailure(QStringLiteral("setup:") + ip);
    audit(QStringLiteral("setup-failed"), who(QString(), ip));
}

QString AccessControl::setupAdmin(const QString &name, const QString &passwordHash, const QString &callsign,
                                  const QString &ip, QString *error)
{
    if (hasAdmin() && !recovery_) {
        if (error) *error = QStringLiteral("an admin already exists");
        return QString();
    }
    if (passwordHash.isEmpty()) {
        if (error) *error = QStringLiteral("no password");
        return QString();
    }
    if (!validCallsign(callsign)) {
        if (error) *error = QStringLiteral("callsign: letters, digits and '/' only");
        return QString();
    }
    QString id;
    const int existing = findUserByName(name);
    if (existing >= 0) {
        // Recovery of a known account: new password, admin, enabled.  Its
        // API tokens go too: one made by whoever had the account before the
        // reset would still answer as it, now an admin.
        AccessUser &u = users_[existing];
        u.passwordHash = passwordHash;
        u.role = Access::Role::Admin;
        u.disabled = false;
        u.tokens.clear();
        if (!callsign.trimmed().isEmpty()) u.callsign = normalizeCallsign(callsign);
        id = u.id;
        dropSessionsOf(id);
        saveUsers();
        audit(QStringLiteral("setup-reset"), who(u.name, ip), QStringLiteral("account reset to admin"));
    } else {
        id = addUser(name, passwordHash, Access::Role::Admin, callsign, error);
        if (id.isEmpty()) return QString();
        audit(QStringLiteral("setup"), who(name, ip), QStringLiteral("admin created"));
    }
    clearFailures(QStringLiteral("setup:") + ip);
    // Locked out is what recovery is for: the misses that led here are
    // forgotten, or the new password was refused for another quarter hour.
    const QString lower = name.toLower();
    clearFailures(QStringLiteral("name:") + lower);
    clearFailures(QStringLiteral("name:") + lower + '|' + ipKey(ip));
    clearFailures(ipKey(ip));
    recovery_ = false;
    return id;
}

// --- lockout ---------------------------------------------------------------

bool AccessControl::lockedOut(const QString &key, int *retryAfterSecs) const
{
    const auto it = failures_.constFind(key);
    if (it == failures_.constEnd()) return false;
    const qint64 nowMs = now().toMSecsSinceEpoch();
    if (it->lockedUntilMs <= nowMs) return false;
    if (retryAfterSecs) *retryAfterSecs = int((it->lockedUntilMs - nowMs + 999) / 1000);
    return true;
}

// An IPv6 client has a /64 of addresses to itself: counted by address it
// had five tries from each.
QString AccessControl::ipKey(const QString &ip)
{
    const QHostAddress a(ip);
    if (!a.isNull() && a.protocol() == QAbstractSocket::IPv6Protocol) {
        bool v4 = false;
        a.toIPv4Address(&v4);
        if (!v4) {
            const Q_IPV6ADDR raw = a.toIPv6Address();
            QByteArray net(reinterpret_cast<const char *>(raw.c), 8);
            return QStringLiteral("ip:") + QString::fromLatin1(net.toHex()) + QStringLiteral("/64");
        }
    }
    return QStringLiteral("ip:") + ip;
}

void AccessControl::recordFailure(const QString &key, int threshold)
{
    const qint64 nowMs = now().toMSecsSinceEpoch();
    Failures &f = failures_[key];
    if (f.count == 0 || nowMs - f.firstMs > qint64(kLockoutWindowSecs) * 1000) {
        f.count = 0;
        f.firstMs = nowMs;
    }
    f.count++;
    if (f.count >= threshold) {
        const int over = f.count - threshold;                 // 0 on the fifth failure
        qint64 secs = qint64(kLockoutBaseSecs) << qMin(over, 20);
        secs = qMin<qint64>(secs, kLockoutMaxSecs);
        f.lockedUntilMs = nowMs + secs * 1000;
        f.firstMs = nowMs;   // keep counting from here: the next miss doubles the lock
    }
}

void AccessControl::clearFailures(const QString &key)
{
    failures_.remove(key);
}

// --- sessions --------------------------------------------------------------

void AccessControl::ensureDecoy()
{
    if (decoyHash_.isEmpty()) decoyHash_ = hashPassword(QString::fromLatin1(randomToken()), iterations_);
}

// Three counters stand behind a login.  The name from this address and the
// address itself lock after five misses.  The name from every address
// together locks after fifty: it used to be five too, which let anyone who
// had read a user name off the seat strip keep that account from logging
// in anywhere, for as long as they cared to send one wrong password a lock
// period (2026-11-21).  Fifty is still nothing to a password of eight
// characters, and a guesser spread over many addresses meets it.
AccessControl::LoginCheck AccessControl::loginPrecheck(const QString &name, const QString &ip) const
{
    LoginCheck c;
    const QString lower = name.toLower();
    int r1 = 0, r2 = 0, r3 = 0;
    const bool valid = validName(name);
    const bool lockedHere = valid && lockedOut(QStringLiteral("name:") + lower + '|' + ipKey(ip), &r1);
    const bool lockedName = valid && lockedOut(QStringLiteral("name:") + lower, &r2);
    const bool lockedIp = lockedOut(ipKey(ip), &r3);
    if (lockedHere || lockedName || lockedIp) {
        c.locked = true;
        c.retryAfterSecs = qMax(r1, qMax(r2, r3));
        return c;
    }
    const int i = valid ? findUserByName(name) : -1;
    if (i >= 0 && !users_.at(i).passwordHash.isEmpty()) {
        c.hash = users_.at(i).passwordHash;
    } else {
        // Unknown name: verify against a decoy so the reply takes as long as
        // for a real account.  ensureDecoy() made it at start-up; this is
        // for a caller that did not ask.
        if (decoyHash_.isEmpty()) const_cast<AccessControl *>(this)->ensureDecoy();
        c.hash = decoyHash_;
    }
    return c;
}

AccessControl::LoginResult AccessControl::loginFinish(const QString &name, bool passwordOk, const QString &ip,
                                                      const QString &userAgent, QByteArray *token,
                                                      int *retryAfterSecs)
{
    // A name no account can have (too long, a newline, anything that is not
    // a user name) is a miss for the address and nothing else: it is not
    // kept as a key and it is not written to the audit log, where a line
    // break in it forged a line and 16 MB of it was 16 MB (2026-11-21).
    const bool valid = validName(name);
    const QString lower = name.toLower();
    const QString hereKey = QStringLiteral("name:") + lower + '|' + ipKey(ip);
    const QString nameKey = QStringLiteral("name:") + lower;
    const QString addrKey = ipKey(ip);
    int retry = 0;
    if ((valid && (lockedOut(hereKey, &retry) || lockedOut(nameKey, &retry))) || lockedOut(addrKey, &retry)) {
        if (retryAfterSecs) *retryAfterSecs = retry;
        return LoginResult::Locked;
    }

    const int i = valid ? findUserByName(name) : -1;
    if (!passwordOk || i < 0) {
        if (valid) {
            recordFailure(hereKey);
            recordFailure(nameKey, kLockoutNameFailures);
        }
        recordFailure(addrKey);
        int r1 = 0, r2 = 0, r3 = 0;
        const bool locked = (valid && (lockedOut(hereKey, &r1) | lockedOut(nameKey, &r3))) | lockedOut(addrKey, &r2);
        audit(locked ? QStringLiteral("login-locked") : QStringLiteral("login-failed"),
              who(valid ? name : QStringLiteral("?"), ip));
        if (locked) {
            if (retryAfterSecs) *retryAfterSecs = qMax(r1, qMax(r2, r3));
            return LoginResult::Locked;
        }
        return LoginResult::BadCredentials;
    }

    AccessUser &u = users_[i];
    if (u.disabled) {
        audit(QStringLiteral("login-disabled"), who(u.name, ip));
        return LoginResult::Disabled;
    }

    // (The address is cleared too.  Keeping its count would stop a member
    // from trying a few passwords on other names between logins of their
    // own, but those names have their own counter for this address, and a
    // club behind one address would lock itself out on five typos a
    // quarter of an hour, whoever made them.)
    clearFailures(hereKey);
    clearFailures(nameKey);
    clearFailures(addrKey);
    // No more than kMaxSessionsPerUser: a loop of logins grew sessions.json
    // without end, all of it rewritten at every login.
    {
        QList<AccessSession> mine;
        for (const AccessSession &x : qAsConst(sessions_))
            if (x.userId == u.id) mine.append(x);
        if (mine.size() >= kMaxSessionsPerUser) {
            std::sort(mine.begin(), mine.end(), [](const AccessSession &a, const AccessSession &b) { return a.lastSeen < b.lastSeen; });
            for (int k = 0; k <= mine.size() - kMaxSessionsPerUser; k++) sessions_.remove(mine.at(k).hash);
        }
    }
    const QByteArray t = randomToken();
    AccessSession s;
    s.hash = tokenHash(t);
    s.userId = u.id;
    s.created = now();
    s.lastSeen = s.created;
    s.ip = ip;
    s.userAgent = userAgent.left(200);
    sessions_.insert(s.hash, s);
    u.lastLogin = s.created;
    saveUsers();
    saveSessions();
    audit(QStringLiteral("login"), who(u.name, ip), Access::roleName(u.role));
    if (token) *token = t;
    return LoginResult::Ok;
}

bool AccessControl::passwordChangeLocked(const QString &userId, int *retryAfterSecs) const
{
    return lockedOut(QStringLiteral("pw:") + userId, retryAfterSecs);
}

void AccessControl::passwordChangeFailed(const QString &userId, const QString &name, const QString &ip)
{
    recordFailure(QStringLiteral("pw:") + userId);
    audit(QStringLiteral("password-change-failed"), who(name, ip), QStringLiteral("wrong current password"));
}

void AccessControl::passwordChangeOk(const QString &userId)
{
    clearFailures(QStringLiteral("pw:") + userId);
}

void AccessControl::logoutOtherSessions(const QString &userId, const QByteArray &keepHash)
{
    bool changed = false;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->userId == userId && it->hash != keepHash) { it = sessions_.erase(it); changed = true; }
        else ++it;
    }
    if (changed) saveSessions();
}

void AccessControl::clearTokens(const QString &userId)
{
    const int i = findUser(userId);
    if (i < 0 || users_.at(i).tokens.isEmpty()) return;
    const int n = users_.at(i).tokens.size();
    users_[i].tokens.clear();
    saveUsers();
    audit(QStringLiteral("token-revoked"), who(users_.at(i).name, QString()), QString("all %1, with the password").arg(n));
}

AccessPrincipal AccessControl::principalFor(const AccessUser &u) const
{
    AccessPrincipal p;
    p.authenticated = true;
    p.userId = u.id;
    p.name = u.name;
    p.callsign = u.callsign;
    p.onAirOwn = u.onAirOwn;
    p.role = u.role;
    return p;
}

AccessPrincipal AccessControl::resolveSession(const QByteArray &token)
{
    if (token.isEmpty()) return AccessPrincipal();
    const QByteArray hash = tokenHash(token);
    auto it = sessions_.find(hash);
    if (it == sessions_.end()) return AccessPrincipal();
    const QDateTime t = now();
    if (it->lastSeen.daysTo(t) >= kSessionIdleDays) {
        sessions_.erase(it);
        sessionsDirty_ = true;
        return AccessPrincipal();
    }
    const int i = findUser(it->userId);
    if (i < 0 || users_.at(i).disabled) {
        sessions_.erase(it);
        sessionsDirty_ = true;
        return AccessPrincipal();
    }
    if (it->lastSeen.msecsTo(t) >= kTouchIntervalMs) {
        it->lastSeen = t;
        sessionsDirty_ = true;
    }
    AccessPrincipal p = principalFor(users_.at(i));
    p.via = QStringLiteral("session");
    p.sessionHash = hash;
    return p;
}

AccessPrincipal AccessControl::principalForSessionHash(const QByteArray &hash) const
{
    const auto it = sessions_.constFind(hash);
    if (it == sessions_.constEnd()) return AccessPrincipal();
    const int i = findUser(it->userId);
    if (i < 0 || users_.at(i).disabled) return AccessPrincipal();
    AccessPrincipal p = principalFor(users_.at(i));
    p.via = QStringLiteral("session");
    p.sessionHash = hash;
    return p;
}

void AccessControl::logout(const QByteArray &token)
{
    if (!token.isEmpty()) logoutSession(tokenHash(token));
}

void AccessControl::logoutSession(const QByteArray &hash)
{
    const auto it = sessions_.find(hash);
    if (it == sessions_.end()) return;
    const int i = findUser(it->userId);
    audit(QStringLiteral("logout"), who(i >= 0 ? users_.at(i).name : it->userId, it->ip));
    sessions_.erase(it);
    saveSessions();
}

void AccessControl::logoutUser(const QString &userId)
{
    dropSessionsOf(userId);
}

QList<AccessSession> AccessControl::sessions() const
{
    return sessions_.values();
}

void AccessControl::expireSessions()
{
    const QDateTime t = now();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->lastSeen.daysTo(t) >= kSessionIdleDays || findUser(it->userId) < 0) {
            it = sessions_.erase(it);
            sessionsDirty_ = true;
        } else {
            ++it;
        }
    }
    if (sessionsDirty_) saveSessions();
    if (usersDirty_) saveUsers();
    // Misses that no longer count: every failed name and address used to
    // stay for the life of the process.
    const qint64 nowMs = t.toMSecsSinceEpoch();
    for (auto it = failures_.begin(); it != failures_.end();) {
        if (it->lockedUntilMs <= nowMs && nowMs - it->firstMs > qint64(kLockoutWindowSecs) * 1000) it = failures_.erase(it);
        else ++it;
    }
}

// --- API tokens --------------------------------------------------------------

QString AccessControl::createToken(const QString &userId, const QString &name, QByteArray *token, QString *error)
{
    const int i = findUser(userId);
    if (i < 0) {
        if (error) *error = QStringLiteral("no such user");
        return QString();
    }
    if (users_.at(i).tokens.size() >= 20) {
        if (error) *error = QStringLiteral("too many tokens for this user");
        return QString();
    }
    const QByteArray t = randomToken();
    AccessToken tok;
    tok.id = randomId(4);
    tok.name = name.trimmed().left(64);
    tok.hash = tokenHash(t);
    tok.created = now();
    users_[i].tokens.append(tok);
    saveUsers();
    audit(QStringLiteral("token-created"), who(users_.at(i).name, QString()), tok.id);
    if (token) *token = t;
    return tok.id;
}

bool AccessControl::revokeToken(const QString &userId, const QString &tokenId)
{
    const int i = findUser(userId);
    if (i < 0) return false;
    QList<AccessToken> &toks = users_[i].tokens;
    for (int k = 0; k < toks.size(); k++) {
        if (toks.at(k).id == tokenId) {
            toks.removeAt(k);
            saveUsers();
            audit(QStringLiteral("token-revoked"), who(users_.at(i).name, QString()), tokenId);
            return true;
        }
    }
    return false;
}

AccessPrincipal AccessControl::resolveToken(const QByteArray &token)
{
    if (token.isEmpty()) return AccessPrincipal();
    const QByteArray hash = tokenHash(token);
    for (AccessUser &u : users_) {
        for (AccessToken &t : u.tokens) {
            if (!constantTimeEqual(t.hash, hash)) continue;
            if (u.disabled) return AccessPrincipal();
            // lastUsed is informational; saved by the next expireSessions()
            // sweep rather than on every request.
            t.lastUsed = now();
            usersDirty_ = true;
            AccessPrincipal p = principalFor(u);
            p.via = QStringLiteral("token");
            return p;
        }
    }
    return AccessPrincipal();
}

// --- audit -------------------------------------------------------------------

// One event, one line.  Every field may carry text somebody outside chose
// (a login name, an address out of X-Forwarded-For, a callsign): nothing
// that breaks a line or a field gets through, and nothing is unbounded.
static QString auditField(const QString &in, int max)
{
    QString out;
    out.reserve(qMin(in.size(), max));
    for (const QChar ch : in) {
        if (out.size() >= max) break;
        out.append(ch.isSpace() || !ch.isPrint() ? QLatin1Char('_') : ch);
    }
    return out;
}

void AccessControl::audit(const QString &event, const QString &whoStr, const QString &detail)
{
    QFile f(QDir(dir_).filePath(QStringLiteral("audit.log")));
    const bool fresh = !f.exists();
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (!auditWarned_) {
            auditWarned_ = true;
            qWarning().noquote() << "Shared: cannot write" << f.fileName() << "-" << f.errorString()
                                 << "- the audit log is NOT being kept";
        }
        return;
    }
    auditWarned_ = false;
    if (fresh) f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QString line = now().toUTC().toString(Qt::ISODate) + ' ' + auditField(event, 40) + ' ' + auditField(whoStr, 120);
    if (!detail.isEmpty()) line += ' ' + detail.simplified().left(400);
    f.write(line.toUtf8() + '\n');
}

// ---------------------------------------------------------------------------
// Reservations
// ---------------------------------------------------------------------------

QString Reservation::repeatName(Repeat r)
{
    switch (r) {
    case Daily:  return QStringLiteral("daily");
    case Weekly: return QStringLiteral("weekly");
    case Once:   break;
    }
    return QStringLiteral("once");
}

Reservation::Repeat Reservation::repeatFromName(const QString &name, bool *ok)
{
    const QString n = name.trimmed().toLower();
    if (ok) *ok = true;
    if (n.isEmpty() || n == QLatin1String("once")) return Once;
    if (n == QLatin1String("daily")) return Daily;
    if (n == QLatin1String("weekly")) return Weekly;
    if (ok) *ok = false;
    return Once;
}

QJsonObject Reservation::toJson() const
{
    QJsonObject o{{"id", id}, {"userId", userId}, {"title", title}, {"start", isoUtc(start)},
                  {"minutes", minutes}, {"repeat", repeatName(repeat)}, {"createdBy", createdBy},
                  {"created", isoUtc(created)}};
    if (until.isValid()) o["until"] = isoUtc(until);
    return o;
}

Reservation Reservation::fromJson(const QJsonObject &o)
{
    Reservation r;
    r.id = o["id"].toString();
    r.userId = o["userId"].toString();
    r.title = o["title"].toString();
    r.start = fromIso(o["start"]);
    r.minutes = o["minutes"].toInt(60);
    r.repeat = repeatFromName(o["repeat"].toString());
    r.until = fromIso(o["until"]);
    r.createdBy = o["createdBy"].toString();
    r.created = fromIso(o["created"]);
    return r;
}

Reservations::Reservations(const QString &dir) : dir_(dir) {}

bool Reservations::load(QString *error)
{
    items_.clear();
    QFile f(QDir(dir_).filePath(QStringLiteral("reservations.json")));
    if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("cannot read %1").arg(f.fileName());
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QString("%1 is not valid JSON").arg(f.fileName());
        return false;
    }
    const QJsonArray arr = doc.object()["reservations"].toArray();
    for (const QJsonValue &v : arr) {
        const Reservation r = Reservation::fromJson(v.toObject());
        if (!r.id.isEmpty() && !r.userId.isEmpty() && r.start.isValid() && r.minutes > 0) items_.append(r);
    }
    return true;
}

bool Reservations::save()
{
    QJsonArray arr;
    for (const Reservation &r : items_) arr.append(r.toJson());
    QSaveFile f(QDir(dir_).filePath(QStringLiteral("reservations.json")));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"reservations", arr}}).toJson(QJsonDocument::Indented));
    return f.commit();
}

Reservation Reservations::get(const QString &id, bool *found) const
{
    for (const Reservation &r : items_) {
        if (r.id == id) { if (found) *found = true; return r; }
    }
    if (found) *found = false;
    return Reservation();
}

bool Reservations::overlaps(const Occurrence &a, const Occurrence &b)
{
    return a.start < b.end && b.start < a.end;
}

// Occurrences of r that touch [from, to].  A repeat steps a day or a week
// from the first start; the arithmetic skips straight to the first one
// that can touch the window, so a year of a daily slot is 365 steps at
// most, not a count from the beginning of time.
QList<Occurrence> Reservations::expand(const Reservation &r, const QDateTime &from, const QDateTime &to)
{
    QList<Occurrence> out;
    if (!r.start.isValid() || r.minutes <= 0 || !from.isValid() || !to.isValid() || to < from) return out;
    const qint64 lenSecs = qint64(r.minutes) * 60;
    const qint64 stepSecs = r.repeat == Reservation::Daily ? 86400 : r.repeat == Reservation::Weekly ? 7 * 86400 : 0;
    const qint64 first = r.start.toSecsSinceEpoch();
    const qint64 fromS = from.toSecsSinceEpoch(), toS = to.toSecsSinceEpoch();
    const qint64 lastStart = r.until.isValid() ? r.until.toSecsSinceEpoch() : std::numeric_limits<qint64>::max();
    qint64 k = 0;
    if (stepSecs > 0 && fromS - lenSecs > first) k = (fromS - lenSecs - first) / stepSecs;
    for (;; k++) {
        const qint64 s = first + k * stepSecs;
        if (s > toS || s > lastStart) break;
        if (s + lenSecs >= fromS) {
            Occurrence o;
            o.res = r;
            o.start = QDateTime::fromSecsSinceEpoch(s, Qt::UTC);
            o.end = QDateTime::fromSecsSinceEpoch(s + lenSecs, Qt::UTC);
            out.append(o);
        }
        if (stepSecs == 0) break;
    }
    return out;
}

QList<Occurrence> Reservations::occurrences(const QDateTime &from, const QDateTime &to) const
{
    QList<Occurrence> out;
    for (const Reservation &r : items_) out.append(expand(r, from, to));
    std::sort(out.begin(), out.end(), [](const Occurrence &a, const Occurrence &b) { return a.start < b.start; });
    return out;
}

Occurrence Reservations::activeAt(const QDateTime &t) const
{
    for (const Reservation &r : items_) {
        const QList<Occurrence> occ = expand(r, t, t);
        for (const Occurrence &o : occ)
            if (o.start <= t && t < o.end) return o;
    }
    return Occurrence();
}

Occurrence Reservations::nextAfter(const QDateTime &t, int withinSecs) const
{
    Occurrence best;
    const QList<Occurrence> occ = occurrences(t.addSecs(1), t.addSecs(withinSecs));
    for (const Occurrence &o : occ) {
        if (o.start <= t) continue;
        if (!best.valid() || o.start < best.start) best = o;
    }
    return best;
}

QString Reservations::add(Reservation r, QString *error, QString *conflict, const QDateTime &now)
{
    if (!r.start.isValid()) {
        if (error) *error = QStringLiteral("a start time is required");
        return QString();
    }
    if (r.minutes < 5 || r.minutes > 24 * 60) {
        if (error) *error = QStringLiteral("length: 5 minutes to 24 hours");
        return QString();
    }
    if (r.userId.isEmpty()) {
        if (error) *error = QStringLiteral("an account is required");
        return QString();
    }
    if (r.until.isValid() && r.until < r.start) {
        if (error) *error = QStringLiteral("until: not before the start");
        return QString();
    }
    r.start = r.start.toUTC();
    if (r.until.isValid()) r.until = r.until.toUTC();
    r.id = AccessControl::randomId(4);
    if (!r.created.isValid()) r.created = QDateTime::currentDateTimeUtc();

    if (items_.size() >= kMaxReservations) {
        if (error) *error = QString("no more than %1 reserved slots: remove one that is over").arg(kMaxReservations);
        return QString();
    }
    // No two slots overlap: every occurrence of the new one in the coming
    // year against every occurrence of the others.  The year runs from now
    // when the first start is in the past: counted from the start, a weekly
    // slot begun two years ago was checked against nothing still to come.
    // Both lists are in time order, so one pass over the two (the product
    // was 134 000 comparisons per daily slot, on the web thread).
    // (`now` is the caller's clock; without one the year runs from the start.)
    if (now.isValid() && r.repeat == Reservation::Once && r.start.addSecs(qint64(r.minutes) * 60) <= now) {
        if (error) *error = QStringLiteral("that slot is already over");
        return QString();
    }
    const QDateTime from = now.isValid() ? qMax(r.start, now.toUTC().addSecs(-qint64(r.minutes) * 60)) : r.start;
    const QDateTime horizon = from.addDays(366 * kMaxYearsAhead);
    const QList<Occurrence> mine = expand(r, from, horizon);
    for (const Reservation &other : items_) {
        const QList<Occurrence> theirs = expand(other, from, horizon);
        int i = 0, j = 0;
        while (i < mine.size() && j < theirs.size()) {
            const Occurrence &a = mine.at(i);
            const Occurrence &b = theirs.at(j);
            if (overlaps(a, b)) {
                if (conflict)
                    *conflict = QString("%1 %2 (%3)").arg(other.id, b.start.toString(Qt::ISODate), Reservation::repeatName(other.repeat));
                if (error) *error = QStringLiteral("that time is taken");
                return QString();
            }
            if (a.end <= b.end) i++; else j++;
        }
    }
    items_.append(r);
    save();
    return r.id;
}

bool Reservations::remove(const QString &id)
{
    for (int i = 0; i < items_.size(); i++) {
        if (items_.at(i).id == id) {
            items_.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

int Reservations::removeUser(const QString &userId)
{
    int n = 0;
    for (int i = items_.size() - 1; i >= 0; i--) {
        if (items_.at(i).userId == userId) { items_.removeAt(i); n++; }
    }
    if (n) save();
    return n;
}
