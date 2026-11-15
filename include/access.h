#ifndef ACCESS_H
#define ACCESS_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <functional>

// Shared Station access control (--shared): who may reach the server, and
// how they prove it.  Pure Qt5Core + QtNetwork (QPasswordDigestor): no
// sockets and no webserver types, so tests/access_test.cpp links it alone and
// webServer only asks questions of it.
//
// One directory holds everything (default <data dir>/shared, created 0700):
//   users.json     accounts: login name, callsign, role, password hash, API tokens
//   sessions.json  browser sessions, keyed by the SHA-256 of their cookie token
//   audit.log      append-only, one line per event (login, logout, setup, lockout, ...)
//
// Passwords are PBKDF2-HMAC-SHA256 with a per-user salt and the iteration
// count stored in the hash, so the default can change without re-hashing
// anyone.  Deriving a key takes hundreds of milliseconds by design: the
// server runs verifyPassword() off its thread (see webServer::authLogin).
// Session cookies and API tokens are 32 random bytes from the OS CSPRNG;
// only their SHA-256 is stored, so a copied sessions.json opens nothing.

namespace Access {
// Ordered: a higher role may do everything a lower one may.
enum class Role { None = 0, Listen = 1, Control = 2, Operate = 3, Admin = 4 };
QString roleName(Role role);                 // "listen" / "control" / "operate" / "admin"
Role roleFromName(const QString &name);      // Role::None for anything else

// What a command needs.  View: hear and watch (listen).  Control: tune and
// set the radio (control).  Tx: anything that puts a signal on the air, and
// the logbook (operate).  Admin: station configuration (admin).
enum class Cap { View = 1, Control = 2, Tx = 3, Admin = 4 };
Role capRole(Cap cap);                       // the lowest role that has it
bool roleAllows(Role role, Cap cap);
// The WebSocket command table (src/access.cpp).  A command not in it is
// Admin: a new command is denied to everyone but admins until classified.
Cap commandCapability(const QString &cmd);
// REST: every GET is View; writes by route.  Unknown writes are Admin.
Cap restCapability(const QString &method, const QString &path);
// The commands a role may not send, for the page to refuse them locally.
QStringList commandsDeniedTo(Role role);
// The seat (phase 3): Control and Tx commands act on the one radio, so
// they need the seat -- except these, which are a browser's own business
// (what it decodes, which meter it watches) and the seat commands themselves.
bool seatExempt(const QString &cmd);
QStringList commandsNeedingSeat();
}

struct AccessToken {
    QString id;             // short random id, what the API refers to
    QString name;           // what the user called it ("logger script")
    QByteArray hash;        // SHA-256 of the token, hex
    QDateTime created;
    QDateTime lastUsed;
};

struct AccessUser {
    QString id;             // random, stable: sessions and tokens point at it
    QString name;           // login name, unique ignoring case
    QString callsign;
    // What the station identifies as while this account has the seat:
    // false = the station's call (a club member), true = the account's own
    // callsign (a licensed guest).  Needs a callsign.  JSON "onAir":
    // "station" | "own".
    bool onAirOwn = false;
    // The admins' own words about the account (a name, a membership number).
    // Admin-only: it is in toJson(), which only the account routes for admins
    // send; the principal, which is what an account sees of itself, has no notes.
    QString notes;
    Access::Role role = Access::Role::Listen;
    QString passwordHash;   // "pbkdf2-sha256$<iterations>$<salt b64>$<key b64>"
    bool disabled = false;
    QDateTime created;
    QDateTime lastLogin;
    QList<AccessToken> tokens;

    QJsonObject toJson(bool withSecrets) const;   // false: the shape the API returns
    static AccessUser fromJson(const QJsonObject &o);
};

struct AccessSession {
    QByteArray hash;        // SHA-256 of the cookie token, hex
    QString userId;
    QDateTime created;
    QDateTime lastSeen;
    QString ip;
    QString userAgent;
};

// Who a request comes from, once its cookie or bearer token is resolved.
struct AccessPrincipal {
    bool authenticated = false;
    QString userId;
    QString name;
    QString callsign;
    bool onAirOwn = false;  // AccessUser::onAirOwn
    Access::Role role = Access::Role::None;
    QString via;            // "session" | "token" | "local"
    QByteArray sessionHash; // set when via == "session"
    QJsonObject toJson() const;
};

class AccessControl
{
public:
    enum class LoginResult { Ok, BadCredentials, Disabled, Locked };

    // PBKDF2 rounds for new passwords: 600k (OWASP's figure for SHA-256)
    // with libcrypto, which does them in ~100 ms; Qt's own implementation is
    // ten times slower, so it gets a lower default rather than a four-second
    // login on a Pi.  Shared/PasswordIterations overrides either.
    static int defaultIterations();
    static bool nativePbkdf2Available();

    static const int kSessionIdleDays = 30;      // a cookie unused this long is dropped
    static const int kLockoutFailures = 5;       // failures in the window before a lock
    static const int kLockoutWindowSecs = 15 * 60;
    static const int kLockoutBaseSecs = 60;      // first lock; doubles per further failure
    static const int kLockoutMaxSecs = 15 * 60;

    explicit AccessControl(const QString &dir);

    // Creates the directory and reads both files.  False = the directory is
    // unusable (not creatable, not writable), with the reason in *error.
    bool load(QString *error = nullptr);
    QString dir() const { return dir_; }

    // --- users --------------------------------------------------------------
    QList<AccessUser> users() const;
    AccessUser user(const QString &id, bool *found = nullptr) const;
    AccessUser userByName(const QString &name, bool *found = nullptr) const;
    int userCount() const { return users_.size(); }
    bool hasAdmin() const;
    // Returns the new id, or empty with *error set (name taken, invalid, ...).
    QString addUser(const QString &name, const QString &passwordHash, Access::Role role,
                    const QString &callsign, QString *error = nullptr);
    // The user list as a file an admin keeps or carries to another station:
    // name, callsign, role, onAir, notes, disabled and the password hash, so
    // the accounts log in there with the passwords they have here.  No ids,
    // no sessions, no API tokens: those belong to this server.
    QJsonObject exportUsers() const;
    // One entry of such a file (or of a hand-written list).  The caller has
    // the hash: the entry's own "passwordHash", or its plain "password"
    // hashed off the server thread.  Never replaces an account: a name
    // already here is refused, like every invalid field.  Returns the new id.
    QString importUser(const QJsonObject &entry, const QString &passwordHash, QString *error = nullptr);
    // Is it a hash verifyPassword() can read?
    static bool validPasswordHash(const QString &hash);
    static constexpr int kMaxImport = 500;
    bool setPasswordHash(const QString &id, const QString &passwordHash);
    bool setRole(const QString &id, Access::Role role);
    bool setCallsign(const QString &id, const QString &callsign);
    // Free text, trimmed, control characters dropped, kMaxNotes at most.
    bool setNotes(const QString &id, const QString &notes);
    static QString normalizeNotes(const QString &notes);
    static constexpr int kMaxNotes = 500;
    // "station" or "own"; own is refused on an account without a callsign.
    bool setOnAir(const QString &id, const QString &onAir, QString *error = nullptr);
    // Does the callsign look like one (letters, digits, '/', a digit and a
    // letter in it)?  Empty is valid: an account need not have one.
    static bool validCallsign(const QString &callsign);
    bool setDisabled(const QString &id, bool disabled);  // disabling ends the user's sessions
    bool removeUser(const QString &id);                   // and the user's sessions
    static bool validName(const QString &name, QString *error = nullptr);
    static bool validPassword(const QString &password, QString *error = nullptr);
    static QString normalizeCallsign(const QString &callsign);

    // --- passwords (slow on purpose: not on the server thread) --------------
    static QString hashPassword(const QString &password, int iterations);
    static bool verifyPassword(const QString &password, const QString &hash);
    int iterations() const { return iterations_; }
    void setIterations(int n) { iterations_ = n; }

    // --- first run / recovery -----------------------------------------------
    // While no admin exists the server shows this code on its console and in
    // its log; the first browser to present it creates the admin account.
    // --shared-reset-admin (enableRecovery) brings the code back with admins
    // present: the name given with it is made an admin with that password,
    // whether it is a new account or a locked-out one.
    bool setupRequired() const { return !hasAdmin(); }
    bool recoveryMode() const { return recovery_; }
    void enableRecovery();
    QString setupCode() const { return (hasAdmin() && !recovery_) ? QString() : setupCode_; }
    void regenerateSetupCode();
    bool setupCodeMatches(const QString &code) const;
    bool setupLocked(const QString &ip, int *retryAfterSecs) const;
    void setupFailed(const QString &ip);
    // Once the code matched (the caller hashed the password): creates the
    // admin, or in recovery resets the named account to an enabled admin
    // with this password.  Ends recovery mode.
    QString setupAdmin(const QString &name, const QString &passwordHash, const QString &callsign,
                       const QString &ip, QString *error = nullptr);

    // --- browser sessions ---------------------------------------------------
    // Login is three steps so the slow one can run on another thread:
    //   1. loginPrecheck(): lockout and account state, and the hash to verify
    //      (a decoy for an unknown name, so the timing does not say which
    //      names exist);
    //   2. verifyPassword(password, check.hash) on a worker;
    //   3. loginFinish(): record the outcome, mint the cookie token on success.
    struct LoginCheck {
        bool locked = false;
        int retryAfterSecs = 0;
        QString hash;       // what to verify the password against
    };
    LoginCheck loginPrecheck(const QString &name, const QString &ip) const;
    LoginResult loginFinish(const QString &name, bool passwordOk, const QString &ip,
                            const QString &userAgent, QByteArray *token, int *retryAfterSecs = nullptr);

    // Resolves a cookie token; touches lastSeen.  Unauthenticated when unknown,
    // expired, or the account is gone or disabled.
    AccessPrincipal resolveSession(const QByteArray &token);
    AccessPrincipal principalForSessionHash(const QByteArray &hash) const;
    void logout(const QByteArray &token);
    void logoutSession(const QByteArray &hash);
    void logoutUser(const QString &userId);   // every session
    QList<AccessSession> sessions() const;
    bool sessionAlive(const QByteArray &hash) const { return sessions_.contains(hash); }
    // Drops idle sessions and writes sessions.json (and users.json, for
    // token lastUsed stamps) if anything changed.  Run it about once a minute.
    void expireSessions();

    // --- API tokens (Authorization: Bearer) ---------------------------------
    QString createToken(const QString &userId, const QString &name, QByteArray *token,
                        QString *error = nullptr);
    bool revokeToken(const QString &userId, const QString &tokenId);
    AccessPrincipal resolveToken(const QByteArray &token);

    // --- audit log ----------------------------------------------------------
    // One line: "<UTC time> <event> <who> <detail>", who = "name@ip" or "-".
    void audit(const QString &event, const QString &who, const QString &detail = QString());
    static QString who(const QString &name, const QString &ip);

    // --- HTTP helpers (pure) ------------------------------------------------
    static QByteArray cookieValue(const QByteArray &cookieHeader, const QByteArray &name);
    static QByteArray bearerToken(const QByteArray &authorizationHeader);
    // The client behind a trusted proxy: the rightmost X-Forwarded-For
    // address that is not itself a trusted proxy.  Cloudflare and nginx
    // append the connecting address to a list the client may have started,
    // so the first entry is the client's to choose and only the last
    // untrusted one is the address that reached the proxy.  An untrusted
    // peer is the client itself, header or not; a list with nothing usable
    // in it falls back to the peer.
    static QHostAddress forwardedClient(const QHostAddress &peer, const QByteArray &forwardedFor,
                                        const QList<QPair<QHostAddress, int>> &trustedProxies);
    static bool inSubnets(const QHostAddress &addr, const QList<QPair<QHostAddress, int>> &subnets);
    static QByteArray randomToken();                   // 32 bytes, base64url, no padding
    static QByteArray tokenHash(const QByteArray &token); // SHA-256, hex
    static QString randomId(int bytes = 8);            // hex

    // --- persistence / testing ------------------------------------------------
    bool saveUsers();
    bool saveSessions();
    void setClock(std::function<QDateTime()> clock) { clock_ = std::move(clock); }

private:
    struct Failures {
        int count = 0;
        qint64 firstMs = 0;        // start of the counting window
        qint64 lockedUntilMs = 0;
    };

    QDateTime now() const;
    int findUser(const QString &id) const;
    int findUserByName(const QString &name) const;
    bool writeJson(const QString &path, const QJsonObject &root) const;
    bool readJson(const QString &path, QJsonObject *root, bool *missing) const;
    void dropSessionsOf(const QString &userId);
    bool lockedOut(const QString &key, int *retryAfterSecs) const;
    void recordFailure(const QString &key);
    void clearFailures(const QString &key);
    AccessPrincipal principalFor(const AccessUser &u) const;

    QString dir_;
    QList<AccessUser> users_;
    QHash<QByteArray, AccessSession> sessions_;   // by hash
    QHash<QString, Failures> failures_;           // by "name:<lower>", "ip:<addr>", "setup:<addr>"
    QString setupCode_;
    QString decoyHash_;
    int iterations_;
    bool recovery_ = false;
    bool sessionsDirty_ = false;
    bool usersDirty_ = false;       // token lastUsed stamps, written by expireSessions()
    std::function<QDateTime()> clock_;
};

// ---------------------------------------------------------------------------
// Reservations (phase 4): who has the seat when.
// ---------------------------------------------------------------------------
// A reservation is a start (UTC), a length, and how it repeats: once, every
// day, or every week at the same time, until a date or for good.  Two of
// them never overlap: add() refuses one that would, naming the other.  The
// seat follows them in webServer::seatTick(): the owner may take the seat
// at will during their slot, is given it at the start when connected, and
// a free seat waits for them for the first part of it.  Kept in
// reservations.json next to users.json.

struct Reservation {
    enum Repeat { Once, Daily, Weekly };
    QString id;
    QString userId;
    QString title;
    QDateTime start;        // UTC; the first occurrence
    int minutes = 60;
    Repeat repeat = Once;
    QDateTime until;        // UTC; no occurrence starts after it (invalid = for good)
    QString createdBy;
    QDateTime created;

    QDateTime end() const { return start.addSecs(qint64(minutes) * 60); }
    QJsonObject toJson() const;
    static Reservation fromJson(const QJsonObject &o);
    static QString repeatName(Repeat r);
    static Repeat repeatFromName(const QString &name, bool *ok = nullptr);
};

struct Occurrence {
    Reservation res;
    QDateTime start;
    QDateTime end;
    bool valid() const { return start.isValid(); }
    QString key() const { return res.id + '@' + start.toUTC().toString(Qt::ISODate); }
};

class Reservations
{
public:
    explicit Reservations(const QString &dir);
    bool load(QString *error = nullptr);
    bool save();
    QList<Reservation> list() const { return items_; }
    Reservation get(const QString &id, bool *found = nullptr) const;
    // Validates (5 min to 24 h, a start, until not before start) and checks
    // every occurrence in the next year against the others.  Returns the
    // new id, or empty with *error (bad input) or *conflict (the other one).
    QString add(Reservation r, QString *error = nullptr, QString *conflict = nullptr);
    bool remove(const QString &id);
    int removeUser(const QString &userId);
    // Every occurrence that overlaps [from, to], soonest first.
    QList<Occurrence> occurrences(const QDateTime &from, const QDateTime &to) const;
    Occurrence activeAt(const QDateTime &t) const;
    // The next occurrence starting after t and no later than t + withinSecs.
    Occurrence nextAfter(const QDateTime &t, int withinSecs) const;
    static QList<Occurrence> expand(const Reservation &r, const QDateTime &from, const QDateTime &to);
    static bool overlaps(const Occurrence &a, const Occurrence &b);
    static const int kMaxYearsAhead = 1;

private:
    QString dir_;
    QList<Reservation> items_;
};

#endif // ACCESS_H
