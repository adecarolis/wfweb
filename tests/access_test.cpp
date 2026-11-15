// Unit test for the Shared Station access control (include/access.h):
// password hashing, accounts and their files, the first-run setup code,
// login lockout, browser sessions and API tokens, the audit log.  Built and
// run by tests/test_cpp_units.py; a low iteration count keeps it quick.
#include "access.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPasswordDigestor>
#include <QTemporaryDir>
#include <cstdlib>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) { qCritical() << "FAIL:" << message; ::exit(1); }
}

const int kIters = 2000;

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

// Login as the three-step dance the server performs.
AccessControl::LoginResult login(AccessControl &ac, const QString &name, const QString &password,
                                 const QString &ip, QByteArray *token = nullptr, int *retry = nullptr)
{
    const AccessControl::LoginCheck check = ac.loginPrecheck(name, ip);
    if (check.locked) {
        if (retry) *retry = check.retryAfterSecs;
        return AccessControl::LoginResult::Locked;
    }
    const bool ok = AccessControl::verifyPassword(password, check.hash);
    return ac.loginFinish(name, ok, ip, "test-agent", token, retry);
}
} // namespace

int main()
{
    // --- roles ---
    require(Access::roleFromName("Admin") == Access::Role::Admin, "role names are case-insensitive");
    require(Access::roleFromName("root") == Access::Role::None, "unknown role is None");
    require(Access::Role::Operate > Access::Role::Control && Access::Role::Control > Access::Role::Listen,
            "roles are ordered");
    require(Access::roleName(Access::roleFromName("operate")) == "operate", "role name round trip");

    // --- capabilities ---
    using Access::Cap;
    require(Access::roleAllows(Access::Role::Listen, Cap::View) && !Access::roleAllows(Access::Role::Listen, Cap::Control),
            "a listener watches, does not tune");
    require(Access::roleAllows(Access::Role::Control, Cap::Control) && !Access::roleAllows(Access::Role::Control, Cap::Tx),
            "control tunes, does not transmit");
    require(Access::roleAllows(Access::Role::Operate, Cap::Tx) && !Access::roleAllows(Access::Role::Operate, Cap::Admin),
            "operate transmits, does not administer");
    require(Access::roleAllows(Access::Role::Admin, Cap::Admin), "admin does everything");
    require(!Access::roleAllows(Access::Role::None, Cap::View), "no role, nothing");
    require(Access::commandCapability("getStatus") == Cap::View, "getStatus is view");
    require(Access::commandCapability("enableAudio") == Cap::View, "a listener may hear");
    require(Access::commandCapability("setFrequency") == Cap::Control, "setFrequency is control");
    require(Access::commandCapability("setPTT") == Cap::Tx, "setPTT is tx");
    require(Access::commandCapability("setTuner") == Cap::Tx, "the antenna tuner transmits");
    require(Access::commandCapability("enableMic") == Cap::Tx, "the mic is tx");
    require(Access::commandCapability("qsoLogged") == Cap::Tx, "logging goes with operating");
    require(Access::commandCapability("setStationCallsign") == Cap::Admin, "the station callsign is admin");
    require(Access::commandCapability("setPower") == Cap::Admin, "rig power is admin");
    require(Access::commandCapability("someNewCommand") == Cap::Admin, "an unknown command fails closed");
    require(Access::restCapability("GET", "/api/v1/radio/ptt") == Cap::View, "every GET is view");
    require(Access::restCapability("PUT", "/api/v1/radio/ptt") == Cap::Tx, "PUT ptt is tx");
    require(Access::restCapability("POST", "/api/v1/radio/cw") == Cap::Tx, "CW is tx");
    require(Access::restCapability("PUT", "/api/v1/radio/frequency") == Cap::Control, "PUT frequency is control");
    require(Access::restCapability("POST", "/api/v1/radio/memories/3/recall") == Cap::Control, "memory recall is control");
    require(Access::restCapability("POST", "/api/v1/radio/memories") == Cap::Admin, "memory write is admin");
    require(Access::restCapability("POST", "/api/v1/logbook") == Cap::Tx, "logbook write is operate");
    require(Access::restCapability("PUT", "/api/v1/logbook/abc") == Cap::Tx && Access::restCapability("DELETE", "/api/v1/logbook/abc") == Cap::Tx
            && Access::restCapability("POST", "/api/v1/logbook/exported") == Cap::Tx, "one's own QSOs are operate");
    require(Access::restCapability("DELETE", "/api/v1/logbook") == Cap::Admin && Access::restCapability("POST", "/api/v1/logbook/adif") == Cap::Admin
            && Access::restCapability("GET", "/api/v1/logbook/adif") == Cap::View, "clearing and importing the whole log is admin");
    require(Access::commandCapability("mergeLogbook") == Cap::Admin, "the browser-log migration is admin");
    require(Access::commandCapability("updateQso") == Cap::Tx && Access::seatExempt("updateQso") && Access::seatExempt("deleteQso")
            && !Access::seatExempt("qsoLogged"), "correcting the log needs no seat, logging does");
    require(Access::restCapability("PUT", "/api/v1/station") == Cap::Admin, "station identity is admin");
    require(Access::restCapability("POST", "/api/v1/new/thing") == Cap::Admin, "unknown write fails closed");
    {
        const QStringList denied = Access::commandsDeniedTo(Access::Role::Listen);
        require(denied.contains("setFrequency") && denied.contains("setPTT") && !denied.contains("getStatus"),
                "denied list for a listener");
        require(Access::commandsDeniedTo(Access::Role::Admin).isEmpty(), "nothing denied to an admin");
        require(!Access::commandsDeniedTo(Access::Role::Operate).contains("setPTT") &&
                Access::commandsDeniedTo(Access::Role::Operate).contains("setPower"), "denied list for an operator");
    }
    require(Access::commandCapability("seatTake") == Cap::Control, "asking for the seat is control");
    require(Access::commandCapability("seatClear") == Cap::Admin, "clearing the seat is admin");
    require(Access::seatExempt("seatTake") && Access::seatExempt("setDigiActive") && !Access::seatExempt("setFrequency"),
            "seat exemptions");
    {
        const QStringList needs = Access::commandsNeedingSeat();
        require(needs.contains("setFrequency") && needs.contains("setPTT") && needs.contains("enableMic"),
                "tuning and keying need the seat");
        require(!needs.contains("getStatus") && !needs.contains("setDigiActive") && !needs.contains("setPower"),
                "watching, decoding and administering do not");
    }

    // --- password hashing ---
    const QString hash = AccessControl::hashPassword("correct horse battery staple", kIters);
    require(hash.startsWith("pbkdf2-sha256$2000$"), "hash names the algorithm and the iterations");
    require(AccessControl::verifyPassword("correct horse battery staple", hash), "right password verifies");
    require(!AccessControl::verifyPassword("Correct horse battery staple", hash), "wrong password fails");
    require(!AccessControl::verifyPassword("x", "garbage"), "malformed hash fails closed");
    require(!AccessControl::verifyPassword("x", "pbkdf2-sha256$0$AA==$AA=="), "zero iterations fails closed");
    require(AccessControl::hashPassword("same", kIters) != AccessControl::hashPassword("same", kIters),
            "every hash gets its own salt");
    {
        // Whatever implementation derived the key (libcrypto when found, Qt
        // otherwise), the hash must verify against Qt's PBKDF2: they are
        // interchangeable across machines.
        const QStringList parts = hash.split('$');
        const QByteArray salt = QByteArray::fromBase64(parts[2].toLatin1());
        const QByteArray key = QByteArray::fromBase64(parts[3].toLatin1());
        const QByteArray qt = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                                                  QByteArray("correct horse battery staple"),
                                                                  salt, kIters, key.size());
        require(qt == key, "native and Qt PBKDF2 agree");
    }
    QString err;
    require(!AccessControl::validPassword("short", &err) && !err.isEmpty(), "short password refused");
    require(AccessControl::validPassword("long enough", nullptr), "8+ characters accepted");
    require(!AccessControl::validName("a", nullptr), "one-letter name refused");
    require(!AccessControl::validName("has space", nullptr), "space in name refused");
    require(AccessControl::validName("k1fm.club-2", nullptr), "dots, dashes, digits accepted");

    // --- HTTP helpers ---
    require(AccessControl::cookieValue("theme=dark; wfweb_session=abc.def-ghi; other=1", "wfweb_session") == "abc.def-ghi",
            "cookie value found among others");
    require(AccessControl::cookieValue("xwfweb_session=no; wfweb_session2=no", "wfweb_session").isEmpty(),
            "cookie name must match exactly");
    require(AccessControl::bearerToken("Bearer  tok123 ") == "tok123", "bearer token trimmed");
    require(AccessControl::bearerToken("bearer tok") == "tok", "bearer scheme is case-insensitive");
    require(AccessControl::bearerToken("Basic dXNlcg==").isEmpty(), "other schemes ignored");
    require(AccessControl::randomToken().size() == 43 && AccessControl::randomToken() != AccessControl::randomToken(),
            "tokens are 32 random bytes, base64url");
    require(AccessControl::tokenHash("a").size() == 64, "token hash is SHA-256 hex");

    // --- the client address behind a proxy (X-Forwarded-For) ---
    {
        using Subnets = QList<QPair<QHostAddress, int>>;
        const Subnets lo{QHostAddress::parseSubnet("127.0.0.1/32")};
        const Subnets lan{QHostAddress::parseSubnet("127.0.0.1/32"), QHostAddress::parseSubnet("10.0.0.0/8")};
        const QHostAddress peer("127.0.0.1"), other("192.0.2.5");
        require(AccessControl::forwardedClient(other, "203.0.113.9", lo) == other, "untrusted peer: header ignored");
        require(AccessControl::forwardedClient(peer, "", lo) == peer, "trusted peer, no header: the peer");
        require(AccessControl::forwardedClient(peer, "203.0.113.9", lo) == QHostAddress("203.0.113.9"), "one address");
        require(AccessControl::forwardedClient(peer, "1.2.3.4, 203.0.113.9", lo) == QHostAddress("203.0.113.9"),
                "Cloudflare / nginx append: the rightmost address, not the client's own first entry");
        require(AccessControl::forwardedClient(peer, "203.0.113.9, 10.0.0.2", lan) == QHostAddress("203.0.113.9"),
                "an inner proxy's own address is skipped");
        require(AccessControl::forwardedClient(peer, "10.0.0.7, 10.0.0.2", lan) == QHostAddress("10.0.0.7"),
                "every entry trusted: the leftmost");
        require(AccessControl::forwardedClient(peer, "unknown, 203.0.113.9:4711", lo) == QHostAddress("203.0.113.9"),
                "garbage skipped, port stripped");
        require(AccessControl::forwardedClient(peer, "[2001:db8::1]:443", lo) == QHostAddress("2001:db8::1"),
                "bracketed IPv6 with a port");
        require(AccessControl::forwardedClient(QHostAddress("::ffff:127.0.0.1"), "203.0.113.9", lo)
                        == QHostAddress("203.0.113.9"), "IPv4-mapped peer matches the v4 block");
        require(AccessControl::forwardedClient(peer, "nonsense", lo) == peer, "nothing usable: the peer");
    }

    QTemporaryDir tmp;
    require(tmp.isValid(), "temporary directory");
    const QString dir = tmp.filePath("shared");

    // --- first run: no users, a setup code, the first admin ---
    {
        AccessControl ac(dir);
        ac.setIterations(kIters);
        require(ac.load(&err), qPrintable("load creates the directory: " + err));
        require(QFileInfo(dir).isDir(), "directory created");
        require(ac.userCount() == 0 && !ac.hasAdmin(), "starts empty");
        const QString code = ac.setupCode();
        require(code.size() == 9 && code[4] == '-', "setup code is XXXX-XXXX");
        require(ac.setupCodeMatches(code.toLower()) && ac.setupCodeMatches(code.mid(0, 4) + code.mid(5)),
                "setup code matches without case or dash");
        require(!ac.setupCodeMatches("AAAA-AAAA") || code == "AAAA-AAAA", "wrong setup code refused");

        const QString adminId = ac.setupAdmin("alain", AccessControl::hashPassword("hunter2hunter2", kIters), "k1fm", "10.0.0.1", &err);
        require(!adminId.isEmpty(), qPrintable("admin created: " + err));
        require(ac.hasAdmin() && ac.setupCode().isEmpty(), "setup code gone once an admin exists");
        require(!ac.setupRequired(), "setup no longer required");
        require(ac.setupAdmin("second", "h", "", "10.0.0.1", &err).isEmpty(), "no second setup");
        require(ac.user(adminId).callsign == "K1FM", "callsign normalized upper-case");
        require(ac.user(adminId).role == Access::Role::Admin, "first user is admin");

        require(ac.addUser("alain", "h", Access::Role::Listen, "", &err).isEmpty() && err.contains("taken"),
                "duplicate name refused");
        require(ac.addUser("ALAIN", "h", Access::Role::Listen, "", &err).isEmpty(), "duplicate ignoring case refused");
        const QString opId = ac.addUser("op", AccessControl::hashPassword("operator-pw", kIters), Access::Role::Operate, "w1aw", &err);
        require(!opId.isEmpty(), "operator added");
        require(ac.userCount() == 2, "two users");

        // Phase 6: what the station identifies as while the account has the seat.
        require(!ac.user(opId).onAirOwn && ac.user(opId).toJson(false)["onAir"].toString() == "station",
                "onAir defaults to the station call");
        require(ac.setCallsign(opId, "") && !ac.setOnAir(opId, "own", &err) && err.contains("callsign"),
                "own is refused without a callsign");
        require(ac.setCallsign(opId, "w1aw"), "callsign back");
        require(!ac.setOnAir(opId, "mine", &err) && err.contains("station or own"), "onAir: only station or own");
        require(!ac.setOnAir("nobody", "own", &err), "onAir: unknown account");
        require(ac.setOnAir(opId, "own", &err) && ac.user(opId).onAirOwn, "own is accepted with a callsign");
        require(ac.user(opId).toJson(false)["onAir"].toString() == "own", "onAir in the API shape");
        require(AccessControl::validCallsign("F/K1FM") && AccessControl::validCallsign("w1abc/p") && AccessControl::validCallsign(""),
                "callsigns: prefix, suffix, none");
        require(!AccessControl::validCallsign("K1 FM") && !AccessControl::validCallsign("K1FM-7") && !AccessControl::validCallsign("/K1FM")
                && !AccessControl::validCallsign("ABC") && !AccessControl::validCallsign("123"), "callsigns: junk refused");
        require(AccessControl::normalizeCallsign(" f/k1fm ") == "F/K1FM", "the callsign keeps its slash");

        // Notes: the admins' free text about an account, kept in users.json.
        require(ac.user(opId).notes.isEmpty() && ac.user(opId).toJson(false)["notes"].toString().isEmpty(), "no notes to begin with");
        require(ac.setNotes(opId, "  Discord 1234567890\n member #42\t "), "notes are set");
        require(ac.user(opId).notes == "Discord 1234567890 member #42", "notes: trimmed, control characters out");
        require(ac.setNotes(opId, QString(2000, 'x')) && ac.user(opId).notes.size() == AccessControl::kMaxNotes, "notes are capped");
        require(!ac.setNotes("nobody", "x"), "notes: unknown account");
        require(ac.setNotes(opId, "member #42"), "notes back");
        {
            AccessControl again(dir);
            again.load();
            require(again.user(opId).notes == "member #42", "notes survive a reload");
        }

        // Export / import: the list travels with its hashes, without ids,
        // sessions or tokens; an import adds and never replaces.
        {
            const QJsonObject file = ac.exportUsers();
            const QJsonArray list = file["users"].toArray();
            require(file["wfwebUsers"].toInt() == 1 && list.size() == ac.userCount(), "export lists every account");
            QJsonObject opEntry;
            for (const QJsonValue &v : list)
                if (v.toObject()["username"].toString() == ac.user(opId).name) opEntry = v.toObject();
            require(opEntry["passwordHash"].toString() == ac.user(opId).passwordHash && opEntry["notes"].toString() == "member #42",
                    "export carries the hash and the notes");
            require(!opEntry.contains("id") && !opEntry.contains("tokens"), "export has no ids and no tokens");

            const QString dir2 = dir + "-import";
            AccessControl other(dir2);
            other.load();
            QString err;
            for (const QJsonValue &v : list)
                require(!other.importUser(v.toObject(), v.toObject()["passwordHash"].toString(), &err).isEmpty(), "import: every exported account");
            require(other.userCount() == ac.userCount(), "import: same number of accounts");
            bool f = false;
            const AccessUser moved = other.userByName(ac.user(opId).name, &f);
            require(f && moved.id != opId && moved.role == ac.user(opId).role && moved.notes == "member #42"
                    && moved.callsign == ac.user(opId).callsign && moved.onAirOwn == ac.user(opId).onAirOwn, "import keeps the account's fields");
            require(moved.passwordHash == ac.user(opId).passwordHash, "import keeps the password");
            require(other.importUser(opEntry, opEntry["passwordHash"].toString(), &err).isEmpty() && err.contains("taken"), "import never replaces an account");
            QJsonObject bad = opEntry;
            bad["username"] = "someone";
            require(other.importUser(bad, "not-a-hash", &err).isEmpty() && err == "no password", "import: a hash it cannot read");
            require(!AccessControl::validPasswordHash("") && !AccessControl::validPasswordHash("pbkdf2-sha256$0$YQ==$YQ==")
                    && AccessControl::validPasswordHash("pbkdf2-sha256$1000$YQ==$YQ=="), "hash shapes");
            bad["role"] = "owner";
            require(other.importUser(bad, opEntry["passwordHash"].toString(), &err).isEmpty() && err.startsWith("role"), "import: unknown role");
            bad["role"] = "listen"; bad["callsign"] = "K1 FM";
            require(other.importUser(bad, opEntry["passwordHash"].toString(), &err).isEmpty() && err.startsWith("callsign"), "import: bad callsign");
            bad["callsign"] = ""; bad["onAir"] = "own"; bad["disabled"] = true;
            const QString sid = other.importUser(bad, opEntry["passwordHash"].toString(), &err);
            require(!sid.isEmpty() && !other.user(sid).onAirOwn && other.user(sid).disabled, "import: own needs a callsign, disabled is kept");
            QDir(dir2).removeRecursively();
        }

        // Files: owner-only, no password or token in the API shape.
        const QFile::Permissions perms = QFileInfo(dir + "/users.json").permissions();
        require(QFile::exists(dir + "/users.json"), "users.json written");
        require(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther)), "users.json is owner-only");
        require(readAll(dir + "/users.json").contains("pbkdf2-sha256$"), "hash is on disk");
        require(!QJsonDocument(ac.user(adminId).toJson(false)).toJson().contains("pbkdf2"), "API shape has no hash");
    }

    // --- reload from disk ---
    {
        AccessControl ac(dir);
        ac.setIterations(kIters);
        require(ac.load(&err), "reload");
        require(ac.userCount() == 2 && ac.hasAdmin(), "users survive a reload");
        bool found = false;
        const AccessUser op = ac.userByName("OP", &found);
        require(found && op.role == Access::Role::Operate && op.callsign == "W1AW", "user found by name, any case");
        require(op.onAirOwn, "onAir survives a reload");
        require(ac.setOnAir(op.id, "station") && !ac.user(op.id).onAirOwn, "onAir back to the station call");
        require(ac.setOnAir(op.id, "own"), "and own again");

        // --- login ---
        QByteArray token;
        require(login(ac, "op", "operator-pw", "10.0.0.2", &token) == AccessControl::LoginResult::Ok, "login ok");
        require(token.size() == 43, "login mints a cookie token");
        require(login(ac, "op", "wrong", "10.0.0.2") == AccessControl::LoginResult::BadCredentials, "wrong password");
        require(login(ac, "nobody", "whatever", "10.0.0.2") == AccessControl::LoginResult::BadCredentials, "unknown user");
        require(!ac.loginPrecheck("nobody", "10.0.0.2").hash.isEmpty(), "unknown user still gets a hash to verify (decoy)");

        AccessPrincipal p = ac.resolveSession(token);
        require(p.authenticated && p.name == "op" && p.role == Access::Role::Operate && p.via == "session",
                "session resolves to its user");
        require(!ac.resolveSession("not-a-token").authenticated, "unknown token is anonymous");
        require(!ac.resolveSession(QByteArray()).authenticated, "empty token is anonymous");
        require(QFile::exists(dir + "/sessions.json"), "sessions persisted");
        require(!readAll(dir + "/sessions.json").contains(token), "sessions.json holds the hash, never the token");

        // Sessions survive a restart.
        {
            AccessControl ac2(dir);
            ac2.setIterations(kIters);
            require(ac2.load(&err), "reload 2");
            require(ac2.resolveSession(token).authenticated, "session survives a restart");
        }

        // Disabling the account ends its sessions and refuses the login.
        require(ac.setDisabled(op.id, true), "disable");
        require(!ac.resolveSession(token).authenticated, "disabled user's session is gone");
        require(login(ac, "op", "operator-pw", "10.0.0.2") == AccessControl::LoginResult::Disabled, "disabled login");
        require(ac.setDisabled(op.id, false), "re-enable");

        // Logout.
        require(login(ac, "op", "operator-pw", "10.0.0.2", &token) == AccessControl::LoginResult::Ok, "login again");
        ac.logout(token);
        require(!ac.resolveSession(token).authenticated, "logged-out token is dead");

        // Idle expiry, with a controllable clock.
        QDateTime fake = QDateTime::currentDateTimeUtc();
        ac.setClock([&fake] { return fake; });
        require(login(ac, "op", "operator-pw", "10.0.0.2", &token) == AccessControl::LoginResult::Ok, "login 3");
        fake = fake.addDays(AccessControl::kSessionIdleDays - 1);
        require(ac.resolveSession(token).authenticated, "session alive a day before expiry");
        fake = fake.addDays(AccessControl::kSessionIdleDays);   // no touch since: now idle too long
        ac.expireSessions();
        require(!ac.resolveSession(token).authenticated, "idle session expired");

        // --- lockout: five misses lock the name and the address ---
        fake = QDateTime::currentDateTimeUtc();
        int retry = 0;
        for (int i = 0; i < AccessControl::kLockoutFailures - 1; i++)
            require(login(ac, "op", "wrong", "10.0.0.3") == AccessControl::LoginResult::BadCredentials, "miss counted");
        require(login(ac, "op", "wrong", "10.0.0.3", nullptr, &retry) == AccessControl::LoginResult::Locked, "fifth miss locks");
        require(retry > 0 && retry <= AccessControl::kLockoutBaseSecs, "retry-after is the base lock");
        require(login(ac, "op", "operator-pw", "10.0.0.3") == AccessControl::LoginResult::Locked, "right password refused while locked");
        require(login(ac, "alain", "hunter2hunter2", "10.0.0.3") == AccessControl::LoginResult::Locked, "the address is locked too");
        require(login(ac, "alain", "hunter2hunter2", "10.0.0.9", &token) == AccessControl::LoginResult::Ok, "another address, another user: fine");
        fake = fake.addSecs(AccessControl::kLockoutBaseSecs + 1);
        require(login(ac, "op", "wrong", "10.0.0.3", nullptr, &retry) == AccessControl::LoginResult::Locked, "a miss after the lock locks again");
        require(retry > AccessControl::kLockoutBaseSecs, "and for twice as long");
        fake = fake.addSecs(AccessControl::kLockoutMaxSecs + 1);
        require(login(ac, "op", "operator-pw", "10.0.0.3", &token) == AccessControl::LoginResult::Ok, "lock expires; success clears it");
        require(login(ac, "op", "wrong", "10.0.0.3") == AccessControl::LoginResult::BadCredentials, "counter was reset by the success");

        // Setup lockout is per address.
        require(!ac.setupLocked("10.0.0.5", &retry), "setup not locked");
        for (int i = 0; i < AccessControl::kLockoutFailures; i++) ac.setupFailed("10.0.0.5");
        require(ac.setupLocked("10.0.0.5", &retry) && retry > 0, "setup attempts lock after five misses");
        require(!ac.setupLocked("10.0.0.6", &retry), "another address is not locked");

        // --- API tokens ---
        QByteArray apiToken;
        const QString tokId = ac.createToken(op.id, "logger", &apiToken, &err);
        require(!tokId.isEmpty() && apiToken.size() == 43, qPrintable("token created: " + err));
        AccessPrincipal tp = ac.resolveToken(apiToken);
        require(tp.authenticated && tp.name == "op" && tp.via == "token", "bearer token resolves");
        require(!ac.resolveToken("Bearer nope").authenticated, "unknown bearer is anonymous");
        require(ac.user(op.id).tokens.size() == 1 && ac.user(op.id).tokens.first().name == "logger", "token listed");
        require(!readAll(dir + "/users.json").contains(apiToken), "users.json holds the token hash only");
        require(ac.revokeToken(op.id, tokId), "revoke");
        require(!ac.resolveToken(apiToken).authenticated, "revoked token is dead");
        require(!ac.revokeToken(op.id, tokId), "revoking twice fails");

        // --- removing a user ends everything of theirs ---
        require(login(ac, "op", "operator-pw", "10.0.0.2", &token) == AccessControl::LoginResult::Ok, "login 4");
        require(ac.removeUser(op.id), "remove");
        require(!ac.resolveSession(token).authenticated, "removed user's session is gone");
        require(ac.userCount() == 1, "one user left");
        require(!ac.removeUser(op.id), "removing twice fails");

        // --- audit log ---
        const QString audit = readAll(dir + "/audit.log");
        require(audit.contains(" setup alain@10.0.0.1"), "setup audited");
        require(audit.contains(" login op@10.0.0.2 operate"), "login audited with the role");
        require(audit.contains(" login-failed op@10.0.0.3"), "miss audited");
        require(audit.contains(" login-locked op@10.0.0.3"), "lock audited");
        require(audit.contains(" logout op@10.0.0.2"), "logout audited");
        require(audit.contains(" token-created op@-"), "token audited");
        const QFile::Permissions aperms = QFileInfo(dir + "/audit.log").permissions();
        require(!(aperms & (QFileDevice::ReadGroup | QFileDevice::ReadOther)), "audit.log is owner-only");
    }

    // --- recovery: --shared-reset-admin brings the setup code back ---
    {
        AccessControl ac(dir);
        ac.setIterations(kIters);
        require(ac.load(&err), "reload for recovery");
        require(ac.hasAdmin() && ac.setupCode().isEmpty(), "no code while an admin exists");
        QByteArray token;
        require(login(ac, "alain", "hunter2hunter2", "10.0.0.1", &token) == AccessControl::LoginResult::Ok, "admin logs in");
        ac.enableRecovery();
        const QString code = ac.setupCode();
        require(!code.isEmpty() && ac.recoveryMode(), "recovery shows a code");
        require(ac.setupCodeMatches(code), "recovery code matches");
        // The existing admin forgot the password: same name, new password.
        const QString id = ac.setupAdmin("alain", AccessControl::hashPassword("new-password-1", kIters), "", "10.0.0.1", &err);
        require(!id.isEmpty(), qPrintable("recovery reset: " + err));
        require(!ac.recoveryMode() && ac.setupCode().isEmpty(), "recovery ends with the reset");
        require(!ac.resolveSession(token).authenticated, "old sessions of the reset account are gone");
        require(login(ac, "alain", "new-password-1", "10.0.0.1") == AccessControl::LoginResult::Ok, "new password works");
        require(login(ac, "alain", "hunter2hunter2", "10.0.0.1") == AccessControl::LoginResult::BadCredentials, "old password is gone");
        require(ac.userCount() == 1, "no account was added");
        // A new name in recovery adds a second admin.
        ac.enableRecovery();
        require(!ac.setupAdmin("rescue", AccessControl::hashPassword("rescue-pw-123", kIters), "", "10.0.0.1", &err).isEmpty(), "recovery adds an admin");
        require(ac.userByName("rescue").role == Access::Role::Admin && ac.userCount() == 2, "second admin");
        require(readAll(dir + "/audit.log").contains(" setup-reset alain@10.0.0.1"), "reset audited");
    }

    // --- reservations: once / daily / weekly, occurrences, conflicts ---
    {
        const QString rdir = tmp.filePath("res");
        QDir().mkpath(rdir);
        Reservations rs(rdir);
        require(rs.load(&err), "reservations load on an empty directory");
        const QDateTime t0 = QDateTime::fromString("2026-11-10T20:00:00Z", Qt::ISODate).toUTC();   // a Tuesday
        Reservation once;
        once.userId = "u1"; once.start = t0; once.minutes = 120; once.title = "net";
        const QString onceId = rs.add(once, &err);
        require(!onceId.isEmpty(), qPrintable("once added: " + err));
        Reservation bad = once;
        bad.minutes = 2;
        require(rs.add(bad, &err).isEmpty() && err.contains("5 minutes"), "too short refused");
        bad = once; bad.start = QDateTime();
        require(rs.add(bad, &err).isEmpty(), "no start refused");

        // Overlaps: inside, straddling the start, straddling the end; adjacent is fine.
        Reservation clash = once;
        clash.userId = "u2"; clash.start = t0.addSecs(30 * 60); clash.minutes = 30;
        QString conflict;
        require(rs.add(clash, &err, &conflict).isEmpty() && conflict.startsWith(onceId), "a slot inside another is refused, naming it");
        clash.start = t0.addSecs(-30 * 60); clash.minutes = 60;
        require(rs.add(clash, &err).isEmpty(), "straddling the start is refused");
        clash.start = t0.addSecs(110 * 60); clash.minutes = 60;
        require(rs.add(clash, &err).isEmpty(), "straddling the end is refused");
        clash.start = t0.addSecs(120 * 60); clash.minutes = 60;
        const QString adjId = rs.add(clash, &err);
        require(!adjId.isEmpty(), "back to back is fine");
        require(rs.remove(adjId), "remove");

        // Weekly, from the Tuesday before, same time: it lands on t0 too.
        Reservation weekly;
        weekly.userId = "u3"; weekly.start = t0.addDays(-7); weekly.minutes = 60; weekly.repeat = Reservation::Weekly;
        require(rs.add(weekly, &err, &conflict).isEmpty() && conflict.startsWith(onceId), "a weekly slot that hits a one-off is refused");
        weekly.start = t0.addDays(-7).addSecs(3 * 3600);   // 23:00 Tuesdays: clear of 20:00-22:00
        const QString weeklyId = rs.add(weekly, &err, &conflict);
        require(!weeklyId.isEmpty(), qPrintable("weekly added: " + err + " " + conflict));
        Reservation daily;
        daily.userId = "u4"; daily.start = t0.addDays(1).addSecs(-14 * 3600); daily.minutes = 30;   // 06:00 every day
        daily.repeat = Reservation::Daily; daily.until = t0.addDays(10);
        const QString dailyId = rs.add(daily, &err, &conflict);
        require(!dailyId.isEmpty(), qPrintable("daily added: " + err + " " + conflict));
        Reservation dailyClash = daily;
        dailyClash.userId = "u5"; dailyClash.start = t0.addDays(3).addSecs(-14 * 3600 + 600);   // 06:10 three days on
        require(rs.add(dailyClash, &err, &conflict).isEmpty() && conflict.startsWith(dailyId), "a one-off inside a daily slot is refused");

        // Occurrences in a window, soonest first.
        const QList<Occurrence> week = rs.occurrences(t0, t0.addDays(7));
        int onceN = 0, weeklyN = 0, dailyN = 0;
        for (const Occurrence &o : week) {
            if (o.res.id == onceId) onceN++;
            else if (o.res.id == weeklyId) weeklyN++;
            else if (o.res.id == dailyId) dailyN++;
        }
        require(onceN == 1 && weeklyN == 1 && dailyN == 7, "one-off once, weekly once (next Tue 23:00 is past the window), daily seven times in a week");
        require(week.first().res.id == onceId, "sorted: the 20:00 slot comes first");
        require(rs.occurrences(t0.addDays(20), t0.addDays(27)).size() == 1, "the daily slot ends at its until date; weekly goes on");

        // Active / next.
        require(rs.activeAt(t0.addSecs(600)).res.id == onceId, "active inside the one-off");
        require(!rs.activeAt(t0.addSecs(2 * 3600)).valid(), "not active at its end");
        require(rs.activeAt(t0.addSecs(3 * 3600 + 60)).res.id == weeklyId, "weekly active on its day");
        require(rs.activeAt(t0.addDays(14).addSecs(3 * 3600 + 60)).res.id == weeklyId, "and two weeks on");
        require(!rs.activeAt(t0.addDays(1).addSecs(3 * 3600 + 60)).valid(), "but not the day after");
        const Occurrence nxt = rs.activeAt(t0.addSecs(-1)).valid() ? Occurrence() : rs.nextAfter(t0.addSecs(-300), 600);
        require(nxt.valid() && nxt.res.id == onceId && nxt.start == t0, "nextAfter finds the slot five minutes ahead");
        require(!rs.nextAfter(t0.addSecs(-3600), 600).valid(), "nothing within ten minutes an hour before");
        require(rs.nextAfter(t0.addSecs(2 * 3600), 3600).res.id == weeklyId, "the next after the one-off is the weekly one");

        // Persistence and removal by account.
        {
            Reservations rs2(rdir);
            require(rs2.load(&err) && rs2.list().size() == 3, "reservations survive a reload");
            require(rs2.get(weeklyId).repeat == Reservation::Weekly && rs2.get(dailyId).until.isValid(), "fields survive");
        }
        require(rs.removeUser("u4") == 1 && rs.list().size() == 2, "removing an account removes its slots");
        require(!rs.remove("nope"), "removing a missing one fails");
        require(Reservation::repeatFromName("WEEKLY") == Reservation::Weekly, "repeat names are case-insensitive");
        bool ok = true;
        Reservation::repeatFromName("monthly", &ok);
        require(!ok, "unknown repeat flagged");
    }

    // --- an unusable directory is reported, not ignored ---
    {
        QFile blocker(tmp.filePath("file"));
        require(blocker.open(QIODevice::WriteOnly), "blocker file");
        blocker.close();
        AccessControl ac(tmp.filePath("file/shared"));
        require(!ac.load(&err) && !err.isEmpty(), "a path under a file cannot be created");
    }

    qInfo() << "access: all checks passed";
    return 0;
}
