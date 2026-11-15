# Shared Station

Accounts and access control for a wfweb server that more than one person
reaches: a club station, a remote station shared with friends, a receiver
anyone may listen to. Off unless asked for; with the flag off, wfweb behaves
exactly as before.

> **Read this first.** Without `--shared`, anyone who can reach the web port
> can key the transmitter, send CW, rewrite the station callsign and the
> logbook. That is fine on a home LAN and nowhere else. If a wfweb server is
> reachable from the internet, run it with `--shared`, and put it behind a
> reverse proxy or a private network (Tailscale, a VPN) as well — see
> *Deployment* below. The station licensee stays the responsible control
> operator whatever the software does; Shared Station gives them accounts,
> an audit log and the means to say who may transmit.

Server build only. The Standalone (browser-only) build drives a USB rig from
the browser it runs in and has nothing to share.

## What it does today

- **Accounts** with a user name, a password, a callsign and an access level
  (`listen`, `control`, `operate`, `admin`).
- **Login** from the page: a session cookie (`HttpOnly`, `SameSite=Strict`,
  `Secure`) that the browser sends on every request, the WebSocket
  included. Sessions last 30 days of inactivity; logging out ends one, an
  admin can end all of a user's.
- **API tokens** for scripts and microcontrollers (`Authorization: Bearer`),
  made from a logged-in browser and shown once.
- **The gate**: with `--shared` on, every `/api/v1/` route but the login
  routes, and every WebSocket, needs an account. The page itself (HTML, JS,
  the decoder models) stays public so the login sheet can load.
- **First run** prints a setup code on the console and in the log; the first
  browser to enter it creates the admin account. No default password.
- **Account management** in the page (the ACCOUNTS tab of the Shared
  station window) and over REST (`/api/v1/shared/users`), admins only.
- **An audit log** (`audit.log`): every setup, login, failed login, lockout,
  logout, connection, token and account change, one line each.
- **Brute-force lockout**: five failed logins for a user name or from an
  address lock it for a minute, doubling on every further miss, 15 minutes
  at most.

- **Access levels, enforced.** Every WebSocket command, every TX audio
  frame and every REST write is checked against the account's level before
  anything happens; the page refuses the same things locally and says why.
- **Visitors**: the admin decides what a browser without a login gets —
  nothing (the login sheet), `listen`, or `control`. Never more without a
  login.
- **TX inhibit**: one switch, and nobody keys the rig — browsers, REST and
  rigctld alike; a key held at that moment is released.
- **TX time-out**: a key held longer than the limit (10 minutes by default)
  is released by the server, whoever holds it.
- **Who is here**: the admin's window lists every connection and can drop
  one; the room has a size (20 connections by default, admins always get in).
- **Every key-down and key-up** goes to the audit log with the account, the
  address, the frequency and mode, and the duration.

- **The seat**: one browser drives the radio at a time. Whoever holds it
  tunes and, with `operate`, keys; everyone else watches. It is taken when
  free, asked for when held (the holder is prompted, an unanswered request
  is granted after a minute), kept for 30 s for a holder whose connection
  dropped, released after 10 minutes without touching the radio, and
  offered down the line of those waiting when it frees. Admins take it
  outright.

- **Reservations**: the admin gives an account the seat at a time — once,
  every day, or every week at the same time, until a date or for good. Two
  slots never overlap. During a slot the owner takes the seat at will, is
  given it at the start when connected, the free seat waits for them for
  the first 15 minutes, and their seat is never handed on unasked. Everyone
  with an account sees the schedule (the SCHEDULE entry in the user menu).

- **Callsigns and the log**: every QSO records who made it (`OPERATOR`)
  next to the call it was made under (`STATION_CALLSIGN`). An account can
  be set to transmit under its own callsign: while it holds the seat the
  station identifies as that call and its QSOs are its own, not the
  club's. Members see the club log and what they logged; they change only
  their own QSOs.

## Reservations

In the Shared station window, the **SCHEDULE** tab: an account (not a listener), a
start — entered and shown in the browser's own time zone; the server keeps
UTC — a length in minutes, once / every day / every week, an optional last
day, a title. A slot that would overlap another, on any of its runs within
a year, is refused with the one in the way. DELETE (two presses) removes
a slot and all its runs; removing an account removes its slots.

While a slot runs:

- its owner may take the seat whoever holds it (the strip reads `SEAT W1ABC` with a **TAKE** button);
- at the start, a holder who is not the owner loses the seat to them if
  they are connected, and is told the slot is theirs if not; five minutes
  before, the holder is warned;
- a free seat is kept for the owner for the first 15 minutes (`SEAT
  reserved for W1ABC` on the strip); after that anyone may take it, and the owner may
  take it back;
- a request from someone else is never granted unasked, and the
  max-time-while-others-wait rule does not apply to the owner; idleness
  still frees the seat.

Weekly and daily slots are anchored to their first start in UTC: a slot
"every Tuesday at 20:00" shifts by an hour across a daylight-saving change
in the browser's zone. Delete and recreate it if that matters.

## Callsigns and the log

Two cases a shared station has to tell apart:

- **A club call shared by many operators.** Every QSO goes out under the
  club call; the log has to say which member made it.
- **A licensed guest on their own call** (a remote station lent to
  members). The guest must identify with their own call, and their QSOs
  are theirs: they do not belong in the club log.

ADIF has the two fields for it, and the server fills them in:

| Case | `STATION_CALLSIGN` | `OPERATOR` | Sent on the air |
|---|---|---|---|
| Club call, many operators | club call | member's own call | club call |
| Guest on their own call | guest's call | guest's call | guest's call |

**The account says which.** Each account has a callsign and an `onAir`
setting, `station` (the default) or `own`, both set by an admin in the
Shared station window's ACCOUNTS tab (the callsign field and the STATION /
OWN button on the account's row). The callsign is a statement about a licence, so it is not
the user's to type. `own` needs a callsign on the account.

**The on-air call follows the seat.** While an `own` account holds the
seat the station is on the air as that account's callsign; at any other
time it is on the station call (the one in Station Settings, an admin's
to change). Every page follows: the CW macros' `[CALL]`, FT8 / FT4, JS8,
RTTY, SSTV and packet all use the on-air call, the callsign fields are
read-only for everyone but an admin, and the strip names the call
(`SEAT yours · F/K1FM`, `SEAT op2 (F/K1FM)`). The server's own users of the
callsign move with it: the RADE end-of-over callsign, the FreeDV and PSK
reporters. A change of the on-air call is one `onair` line in the audit
log. The station call in the settings file never changes because of it:
an admin editing MY CALL while a guest has the seat edits the station
call, and the guest stays on theirs.

With the seat switched off there is no single operator, so everyone
transmits under the station call and `own` has no effect (the SHARED
window says so). An `own` account whose callsign was removed is refused
the seat, with the reason.

**What goes in the log.** When an account logs a QSO the server stamps
three fields and ignores whatever the client sent for them:
`STATION_CALLSIGN` = the call on the air at that moment, `OPERATOR` = the
account's callsign, and the account itself (`APP_WFWEB_USER`, wfweb's own
field, left out of files exported for other services). An edit never
changes the three. A script logging with an API token is stamped the same
way; with the seat free that is the station call, for an `own` account
too, since nobody is on the air under their own call without the seat.

**Who sees what.**

- Admins see every QSO.
- Any account, `listen` included, sees the club log (the QSOs made under
  the station call, and old records with no `STATION_CALLSIGN`) and every
  QSO it logged itself.
- A member's QSOs under their own call are private to that member and the
  admins: they are not sent to the other pages, not listed, not exported.
- Visitors without an account see no log.

The QSO count on each page is the count of what that page may see. The
log view grows OPERATOR and STATION columns once a record names an
operator, and shows Edit / Delete only on records the viewer may change.
The FT8 worked-before hints answer for the call the viewer transmits as:
an `own` account is told what it worked under its own call, everyone
else what the station call worked.

**Who changes what.** A member edits and deletes only the QSOs they
logged (no seat needed for that); admins change any. Records nobody is
recorded as having logged (made before `--shared`, or imported) are the
admins'. Clearing the log, importing an ADIF file and the migration of a
browser's old local log are admin-only. If the station call is changed,
QSOs under the old one stop counting as the club log and are then seen
by the admins and by whoever logged them.

**Export.** "Download new QSOs" is per callsign, as an upload to LoTW is:
an admin gets the club log's new QSOs, a member the new QSOs they made
under their own call (a member who only ever used the club call has
none). The export stamp is put only on records the caller may export.

**The remote logger** (`--remote-log`, the WSJT-X UDP protocol) is the
station's: it receives the QSOs made under the station call, not a
guest's own.

**What the server can and cannot enforce.** Where the callsign passes
through the server it is checked: an APRS beacon's source call and the
AX.25 terminal's own call must be the on-air call (any SSID, `-0` to
`-15`), or the command is refused with the reason. The unattended APRS
beacon keeps the source an admin configured. AX.25 addresses have no
room for a `/`, so a guest on a call like `F/K1FM` has no packet. In the
audio modes (FT8, JS8, CW, RTTY, SSTV, voice) the callsign is in audio
the browser makes, which the server cannot read: the page follows the
on-air call and does not let a member type another, but a modified page
could send anything. Accountability there is the `OPERATOR` field on
what gets logged and the audit log's key-down lines, which name the
account behind every transmission.

## The seat

A thin strip under the top bar says whose seat it is on the left
(`SEAT ...`) and who you are on the right (`YOU ...`). Next to the seat is
the one button that applies:

| Strip | Meaning | Button |
|-------|---------|--------|
| `SEAT free` (green) | nobody has it | **TAKE** |
| `SEAT yours` (green) | you have it | **OPTIONS**: release, go private (if allowed) |
| `SEAT W1ABC` (amber) | W1ABC has it | **ASK** W1ABC for it |
| `SEAT W1ABC · you asked · 45 s` | W1ABC has 45 s to answer | **WITHDRAW** |
| `SEAT W1ABC · you are #2 in line` | you are second in line | **LEAVE LINE** |
| `SEAT free for you · 28 s` | the seat is yours to take for 28 s | **TAKE** (blinking) |
| `SEAT kept for W1ABC · 25 s` | W1ABC dropped off; the seat waits for them | — |
| `SEAT reserved for W1ABC` | a reserved slot is running, its owner is away | — |

Changing something on the radio without the seat does nothing: a note says
why ("Take the seat first", or who has it), and while the seat is free the
**TAKE** button blinks.

`YOU name · role` opens the account menu: SCHEDULE, STATION SETTINGS
(admins) and LOG OUT. A visitor sees `visitor · LOG IN` there.

Asked for the seat, the holder gets a bar with **HAND OVER** / **DECLINE**.
Unanswered, the request is granted when the time runs out (60 s by
default, 0 = only by hand) and never while the holder is transmitting.
Declined, the asker stays in line for when the seat frees. When it does,
the first in line has 30 s to take it before it goes to the next.

A holder whose connection drops (a reload, a phone off the network) keeps
the seat for 30 s; coming back within that, they have it again without
asking. Everyone keeps their level: a `control` holder tunes, an `operate`
holder keys. Scripts with an API token act on the radio only while the
seat is free or held by their own account (`409` otherwise). `rigctld`,
being the station's own software, is not held to the seat.

The seat's rules are in the Shared station window's **RULES** tab: the seat on or off (off: everyone with
the level controls the radio at once, with no seat at all), idle minutes,
seconds before an unanswered request is granted, the most minutes a seat
may be kept while others wait (0 = no limit), whether a higher level may
take the seat over (then a request from one is granted after 30 s),
**LISTEN-IN** (others hear and see while someone has the seat; off, they
get status only) and **PRIVATE ALLOWED** (the holder may switch the others'
audio and scope off from the seat menu). Admins always hear and see.
TAKE IT / CLEAR in the **NOW** tab act on a seat someone else holds.

## Access levels

| Level | May |
|-------|-----|
| `listen` | hear the receiver, see the scope and meters, decode locally (CW, FT8, RTTY, SSTV) |
| `control` | tune, change mode, filters, gains, scope, recall memories, run the digital decoders' settings |
| `operate` | key the rig: PTT, mic, CW, FT8/JS8/RTTY/SSTV/packet transmit, the tuner; log QSOs, change the ones they logged |
| `admin` | everything, plus accounts, the policy, the connection list, the station callsign, CW macros, memories, rig power, LAN connect; any QSO in the log, clearing it, importing into it |

A command the page sends that the level lacks is refused on the page with a
short toast ("Needs control access — log in", "Needs operate access (you
have control)") and, should anything reach the server regardless, refused
there with the same answer. The PTT key is dimmed for anyone who cannot
transmit; the frequency display and MODE for a listener.

## The Shared station window (YOU > STATION SETTINGS, admins)

Four tabs:

- **NOW**: who has the seat (TAKE IT, CLEAR), whether transmitting is
  allowed (INHIBIT TX / ALLOW TX), and who is connected: name or *visitor*,
  level, address, SEAT / ON AIR; KICK ends that connection (the account
  stays — disable it to keep them out).
- **ACCOUNTS**: the callsign, STATION / OWN (what the station identifies
  as while the account has the seat, see "Callsigns and the log"), level, PASSWORD (sets a new one and ends the account's
  sessions), ENABLE / DISABLE, DELETE (two presses), and a form to add one.
- **SCHEDULE**: the reservations, a form to add one, and the next two weeks.
- **RULES**: every setting with a line saying what it does; a change
  applies at once. *Visitors without a login*: REFUSED (the login sheet),
  LISTEN or CONTROL — the strip then shows `visitor · LOG IN`, and the sheet
  it opens can be put away; lowering the level applies to visitors already
  connected, refusing them ends their connections. *Connections at once*,
  *TX time-out* (0 = none), and the seat's rules.

## Setting it up

```bash
wfweb --shared
```

or `Shared/Enabled=true` in the settings file. On the first start the
console status page and the log say:

```
Shared: no admin account yet - open the web page and enter setup code K7QX-4MZP
```

Open the page, enter the code with a user name and a password of at least
eight characters, and you are the admin. Add the other accounts from
`YOU` > STATION SETTINGS > ACCOUNTS, or over REST (below).

### Files

Everything lives in one directory, `<data dir>/shared` by default
(`~/.local/share/wfweb/shared` on Linux), `--shared-dir <dir>` or
`Shared/Dir=` to move it. The directory is created `0700`, the files `0600`.

| File | Holds |
|------|-------|
| `users.json` | accounts: name, callsign, role, password hash (PBKDF2-HMAC-SHA256, per-user salt, iteration count in the hash), API token hashes |
| `sessions.json` | browser sessions by the SHA-256 of their cookie token (the token itself is never stored) |
| `reservations.json` | the reserved slots |
| `audit.log` | one line per event: `2026-11-07T10:11:12Z login clubadmin@192.168.1.20 admin` |

Back up the directory with the rest of the station's data. It holds no
plain-text secret: a copy of it logs nobody in.

### Locked out

```bash
wfweb --shared --shared-reset-admin
```

prints a new setup code. Entering it with an existing user name sets that
account's password and makes it an enabled admin; a new name adds an admin.
The code is valid for that run only, until it is used.

### Settings

| Setting / flag | Meaning |
|----------------|---------|
| `--shared`, `Shared/Enabled=true` | access control on |
| `--shared-dir <dir>`, `Shared/Dir=` | where accounts, sessions and the audit log live |
| `--shared-reset-admin` | setup code to reset or create an admin (this run only) |
| `--trust-proxy <cidr>[,<cidr>]`, `Shared/TrustProxy=` | reverse proxy address(es) whose `X-Forwarded-For` / `X-Forwarded-Proto` are believed (see *Deployment*). The client is the rightmost `X-Forwarded-For` entry that is not itself a trusted proxy: Cloudflare and nginx append to a list the client may have started, so the first entry is never believed |
| `Shared/Anonymous=none\|listen\|control` | what a visitor without a login gets (default `none`); the RULES tab |
| `Shared/TxInhibit=true` | nobody keys the rig (default off); INHIBIT TX in the NOW tab |
| `Shared/TxTimeoutSecs=` | a key held this long is released (default 600, 0 = never) |
| `Shared/MaxClients=` | connections at once, admins excepted (default 20) |
| `Shared/Seat=false` | no seat: everyone with the level controls the radio at once (default on) |
| `Shared/SeatIdleSecs=` | the seat is released after this long without a command (default 600, 0 = never) |
| `Shared/SeatRequestSecs=` | an unanswered request is granted after this (default 60, 0 = only by hand) |
| `Shared/SeatMaxSecs=` | the most a seat is kept while others wait (default 0 = no limit) |
| `Shared/SeatPreemptHigher=true` | a higher level's request is granted after 30 s |
| `Shared/ListenIn=false` | nobody but the holder (and admins) hears or sees while the seat is held |
| `Shared/AllowPrivate=true` | the holder may go private |
| `Shared/PasswordIterations=` | PBKDF2 rounds for new passwords; default 600 000 with libcrypto, 150 000 with Qt's own implementation |
| `--cert <file> --key <file>`, `Web/CertFile=`, `Web/KeyFile=` | serve HTTPS with your own certificate (PEM; the certificate file may carry its chain, the key may be RSA or EC, unencrypted) |

## Deployment

**`--shared` needs TLS.** The session cookie must not travel in clear, so
wfweb refuses to start in shared mode on plain HTTP unless `--trust-proxy`
names a TLS-terminating proxy in front of it. Logins are refused on the
plain-HTTP REST port (8081) whatever the mode; bearer tokens are accepted
there, so keep that port off the internet (it exists for microcontrollers on
the LAN).

Three ways to reach a shared station from outside, best first:

1. **A private network** (Tailscale, WireGuard, ZeroTier). Nothing is
   exposed to the internet; wfweb's own certificate and accounts are enough.
   Tailscale's `tailscale serve` also gives you a trusted certificate.
2. **An identity-aware proxy** (Cloudflare Tunnel + Access, Pangolin). The
   proxy terminates TLS with a real certificate, asks for identity, and
   forwards to wfweb on `localhost:8080`. Run wfweb with
   `--trust-proxy 127.0.0.1` so the audit log sees the visitor's address
   and the cookie learns the connection is HTTPS. Mapping the proxy's
   identity onto wfweb accounts is planned; until then users log in to the
   proxy and then to wfweb.
3. **A port forward** with a real certificate: `--cert fullchain.pem --key
   privkey.pem` from Let's Encrypt (restart wfweb after a renewal), and
   `--shared`. wfweb's HTTP server is small and hand-written; a reverse
   proxy (Caddy, nginx) in front of it is still the better place to meet the
   internet.

Whatever the route, the browser needs a certificate it trusts for the
`Secure` cookie: wfweb's self-signed one works after the warning is
accepted; a real one is better.

## REST

All under `/api/v1/auth/`; JSON in and out. `POST` bodies are JSON objects.

| Route | Who | Does |
|-------|-----|------|
| `GET /auth/me` | anyone | `{shared, authenticated, user, setupRequired, recovery, secure}`; `{shared:false}` without `--shared` |
| `POST /auth/setup` `{code, username, password, callsign}` | first run / recovery | creates the admin, logs them in (sets the cookie) |
| `POST /auth/login` `{username, password}` | anyone, HTTPS only | sets the session cookie; `401` wrong, `403` disabled, `429` + `Retry-After` locked |
| `POST /auth/logout` | session | ends it (and closes its WebSockets), clears the cookie |
| `POST /auth/password` `{current, password}` | session | changes the caller's password |
| `GET /auth/tokens` | session | the caller's API tokens (ids, names, dates) |
| `POST /auth/tokens` `{name}` | session | `{id, name, token}` — the token is shown this once |
| `DELETE /auth/tokens/<id>` | session | revokes it |

Account management, admins only, under `/api/v1/shared/users`:

| Route | Does |
|-------|------|
| `GET` | `{users:[{id, name, callsign, onAir, role, disabled, created, lastLogin, tokens}]}` |
| `POST` `{username, password, role, callsign}` | adds an account (`201`) |
| `PUT /<id>` `{role?, callsign?, onAir?, disabled?, password?, kick?}` | changes it; `disabled`, a new `password` and `kick` end the account's sessions. `onAir` is `station` or `own` (`400` without a callsign on the account); a callsign is letters, digits and `/`. The account's open pages follow at once, and so does the on-air call when it holds the seat |
| `DELETE /<id>` | removes it; not yourself, not the last admin |

Policy and connections, admins only:

| Route | Does |
|-------|------|
| `GET /api/v1/shared/policy` | `{policy:{anonymous, txInhibit, txTimeoutSecs, maxClients}}` |
| `PUT /api/v1/shared/policy` `{anonymous?, txInhibit?, txTimeoutSecs?, maxClients?}` | changes what is given; applied at once, saved to the settings file, pushed to every page as `{type:"sharedPolicy", policy}` |
| `GET /api/v1/shared/connections` | `{connections:[{id, name, anonymous, role, callsign, ip, since, keying, seat}], max}` |
| `DELETE /api/v1/shared/connections/<id>` | ends that connection (not your own) |
| `GET /api/v1/shared/seat` | `{seat:{enabled, holder:{name, id, role, since, private, keying}|null, queue, grace, privateNow, listenIn, allowPrivate, reservation?, next?}}` (any account) |
| `DELETE /api/v1/shared/seat` | frees the seat (admin) |
| `GET /api/v1/shared/reservations` | `{reservations:[{id, userId, name, callsign, title, start, minutes, repeat, until?, createdBy, created}], upcoming:[runs in the next 14 days: {id, name, title, start, end, minutes, repeat}], active}` (any account) |
| `POST /api/v1/shared/reservations` `{username|userId, start (ISO), minutes, repeat: once|daily|weekly, until?, title?}` | adds a slot (`201`); `400` bad input, `409 {conflict}` when it overlaps another |
| `DELETE /api/v1/shared/reservations/<id>` | removes the slot and all its runs |

The `seat` message carries `reservation` (the run in progress: `{id,
name, title, start, end, holdUntil, mine}`) and `next` (a run starting
within the hour).

The seat over the WebSocket: `seatTake` (free: takes it; held: asks for it;
`{force:true}` from an admin takes it), `seatRelease`, `seatHandover
{accept}` (the holder's answer), `seatCancel` (withdraw, leave the line,
pass an offer), `seatAccept` (take an offered seat), `seatPrivate {value}`,
`seatClear` (admin). The server answers every change with a `seat` message
per browser — `{type:"seat", enabled, holder, mine, queue, position,
request?, requested?, offer?, grace?, privateNow, listenIn, allowPrivate}`
— plus `seatLost {reason, to?}` to a holder who lost it and `seatNotice
{text}` for the rest (declined, offer expired, the idle warning).

Every other `/api/v1/` route needs the level in the table above: a `GET`
is `listen`, a write to `/radio/*` is `control` except `/radio/ptt` and
`/radio/cw` (`operate`), the logbook is `operate`, `/station`, `/cw/macros`
and memory writes are `admin`. Short of it: `403 {"error": "needs operate
access", "need": "operate"}`.

The WebSocket says who the browser is as its first message:
`{type:"access", shared, user:{id, name, callsign, role}|null, anonymous,
role, can:{control, tx, admin}, denied:{cmd: level}, policy}` (just
`{shared:false}` in open mode). A browser without a session gets
`{type:"authRequired", setupRequired}` and close code `4401` unless visitors
are admitted; one too many gets `{type:"stationFull", max}` and `4503`. A
refused command is answered `{type:"denied", cmd, need, reason}`.

A script:

```bash
# once, from a logged-in browser session (cookie in $JAR after a login)
curl -k -b $JAR -X POST https://station:8080/api/v1/auth/tokens -d '{"name":"logger"}'
# then, on the LAN
curl -H "Authorization: Bearer <token>" http://station:8081/api/v1/radio/frequency
```

## What else changed with this feature

These apply with or without `--shared`:

- A connection that has not finished sending its request headers after 10 s
  is dropped, as is one that stops reading its response for 60 s; no more
  than 128 HTTP connections are kept at once. A slow but moving download
  (the 27 MB CW model on a weak link) is left alone.
- A WebSocket upgrade whose `Origin` names another host than the one it was
  sent to is refused with `403` (a page on another site cannot drive your
  radio with your browser's cookie). Scripts, which send no `Origin`, are
  unaffected.
- Responses carry `X-Content-Type-Options: nosniff` and `Referrer-Policy:
  no-referrer`; with `--cert/--key`, `Strict-Transport-Security` too (never
  with the self-signed certificate, which would lock the browser out).
- The console status page states the access mode: *open - anyone who
  reaches this port can transmit* without `--shared`.

## On responsibility

The amateur radio rules of every country make the station licensee
responsible for what the station transmits, and require a licensed control
operator at the control point — with wfweb, the browser of the person
operating. Shared Station lets you know who that was (the audit log names
every login, connection, seat change and key-down), lets you take
access away in a second, and keeps the public out. It cannot make an
unlicensed operator legal, and it does not change who answers for the
station. Give `operate` access to licensed people you know; give `listen`
access freely.

Personal data: user names, callsigns and addresses in `audit.log` are
personal data under GDPR and its relatives. Keep the log where only the
station's keeper reads it, and prune it when it is no longer needed.
