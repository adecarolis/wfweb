# Shared Station

Accounts and access control for a wfweb server that more than one person
reaches: a club station, a remote station shared with friends, a receiver
anyone may listen to. Off unless asked for; with the flag off, wfweb behaves
exactly as before.

> **Read this first: a shared station belongs on a local network or behind a
> VPN, not on the open internet.** `--shared` is made for a club or a
> multi-operator station whose members reach it on the station's own network
> or over a private network (Tailscale, WireGuard, the club's VPN). It is
> **not** meant to be exposed to the public internet: do not forward its
> ports from the router and do not run it on a public address. Accounts, the
> seat and the audit log decide who may do what among the people already let
> onto that network; they are not a front door for the whole internet. See
> *Deployment* below.
>
> Without `--shared`, anyone who can reach the web port can key the
> transmitter, send CW, rewrite the station callsign and the logbook. That
> is fine on a home LAN with one operator and nowhere else. The station
> licensee stays the responsible control operator whatever the software
> does; Shared Station gives them accounts, an audit log and the means to
> say who may transmit.

Server build only. The Standalone (browser-only) build drives a USB rig from
the browser it runs in and has nothing to share.

## What it does

- **Accounts** with a user name, a password, a callsign and an access level
  (`listen`, `control`, `operate`, `admin`).
- **Login** from the page: a session cookie (`HttpOnly`, `SameSite=Strict`,
  `Secure`) that the browser sends on every request, the WebSocket
  included. A session ends after 30 days without use, and the browser's
  cookie 30 days after the login, so that is the longest one lasts; logging
  out ends one, an admin can end all of a user's, and an account has twenty
  at most (the oldest makes room).
- **API tokens** for scripts and microcontrollers (`Authorization: Bearer`),
  made with a logged-in session and shown once (one REST call, see *REST*
  below; the page has no button for it), twenty an account at most. A token
  has its account's level and no expiry. What it is for is reading the
  station and logging; what it never does, whatever the level: set the
  radio, manage accounts, change a password, make another token (see *The
  other doors*). A KICK or the account's own password change does not end
  it: revoke it, have an admin set the account a new password (which ends
  its tokens with its sessions), or disable the account.
- **The gate**: with `--shared` on, every `/api/v1/` route but the login
  routes, and every WebSocket, needs an account, unless the admin admits
  visitors (below). The page itself (HTML, JS, the decoder models) stays
  public so the login sheet can load.
- **First run** prints a setup code on the console and in the log; the first
  browser to enter it creates the admin account. No default password.
- **Account management** in the page (the ACCOUNTS tab of the Shared
  station window) and over REST (`/api/v1/shared/users`), admins only. The
  list can be saved to a file and added to from one (EXPORT / IMPORT): a
  backup, or the accounts of another station, passwords included.
- **An audit log** (`audit.log`): every setup, login, failed login, lockout,
  logout, connection, token and account change, one line each.
- **Brute-force lockout**: five failed logins from an address, for one
  user name or for several, lock that address out for a minute, doubling on
  every further miss, 15 minutes at most. A name is locked for the address
  that missed, not for its owner elsewhere; fifty misses for one name from
  all addresses together lock the name for everyone. An IPv6 client counts
  by its /64. The counts are kept in memory: a restart clears them.

- **Access levels, enforced.** Every WebSocket command, every TX audio
  frame and every REST write is checked against the account's level before
  anything happens; the page refuses the same things locally and says why.
- **The radio is operated from the page.** Over REST it is read, never set:
  no tuning, no key, by any account. Scripts read the station and log.
- **Visitors**: the admin decides what a browser without a login gets —
  nothing (the login sheet), `listen`, or `control`. Never more without a
  login.
- **TX inhibit**: one switch, and nothing goes on the air — a browser's
  key, CW, packet and tune, rigctld's key and its CW, the unattended APRS
  beacon, and the frames the packet link layer would send on its own (the
  answer to an inbound connect, acknowledgements, retries). A key held at
  that moment is released and the rig's CW keyer is told to stop. While it
  is on, everything of the `operate` level is refused except logging a QSO,
  the keyer's STOP and switching one's own microphone off: the tuner's
  ON / OFF and the packet terminal's buttons wait until it is lifted.
- **Bands nobody transmits on**: the same refusal for one band or several,
  for everyone at once (admins included). The strip shows `TX INHIBIT · 20m`
  while a key would land on one; tuning and listening there are not
  affected, and a key held while tuning onto one is released. In split the
  band that counts is the one the rig transmits on, not the one it listens
  on.
- **TX time-out**: a key held longer than the limit (10 minutes by default)
  is released by the server, whoever holds it.
- **Who is here**: the admin's window lists every connection and can drop
  one; the room has a size (20 connections by default, admins always get in).
- **Every key-down and key-up** goes to the audit log with the account, the
  address, the frequency and mode (the transmit frequency, marked `split`,
  when the rig is in split), and the duration. What goes on the air without
  a key of its own (CW through the rig's keyer, packet and APRS frames, a
  tune) is a `tx` line naming the account, the kind and the frequency, one a
  minute at most per account and kind, written when the command is taken
  (a refused one leaves no line). rigctld's CW is `tx rigctld@127.0.0.1 cw`,
  and a packet link the station answered on its own is a `tx -@- packet
  link from ... to ...` line.

- **The seat**: one browser drives the radio at a time. Whoever holds it
  tunes and, with `operate`, keys; everyone else watches. It is taken when
  free, asked for when held (the holder is prompted, an unanswered request
  is granted after a minute), kept for 30 s for a logged-in holder whose
  connection dropped, released after 10 minutes in which the holder did
  nothing to the radio, and offered down the line of those waiting when it
  frees. An admin can take it outright from the Shared station window
  (NOW > TAKE IT).

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

In the Shared station window, the **SCHEDULE** tab: an account (not a
listener, not a disabled one), a start — entered and shown in the browser's
own time zone; the server keeps UTC — a length of 5 minutes to 24 hours,
once / every day / every week, an optional last day, a title (80
characters). A slot that would overlap another, on any of its runs within a
year from now, is refused with the one in the way; so is a one-off slot
that is already over. DELETE (two presses) removes a slot and all its
runs; removing an account removes its slots. A station keeps 200 slots at
most.

While a slot runs:

- its owner may take the seat whoever holds it (the strip reads `SEAT W1ABC` with a **TAKE** button);
- at the start, a holder who is not the owner loses the seat to them if
  they are connected, and is told the slot is theirs if not; five minutes
  before, the holder is warned;
- a free seat is kept for the owner for the first 15 minutes (`SEAT
  reserved for W1ABC` on the strip): TAKE is refused to the others, and
  those waiting in line are not offered it (an admin may still take it);
  after that anyone may take it, and the owner may take it back;
- a request from someone else is never granted unasked, and the
  max-time-while-others-wait rule does not apply to the owner; idleness
  still frees the seat.

An owner who may no longer hold the seat (the account's level lowered to
`listen` since, or an own-call account left without a callsign) is treated
as absent. The server's own notes about a slot ("reserved from 20:00 UTC",
the five-minute warning) give the time in UTC; the schedule and the strip
show the browser's zone.

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
admins'. Clearing the log and importing an ADIF file are admin-only. A
browser's old local log is not handed to the server on a shared station
(the records have no account behind them). If the station call is changed,
QSOs under the old one stop counting as the club log and are then seen
by the admins and by whoever logged them; "Download new QSOs" no longer
offers them, the whole-log download still has them.

**Export.** "Download new QSOs" is per callsign, as an upload to LoTW is:
an admin gets the club log's new QSOs, a member the new QSOs they made
under their own call (a member who only ever used the club call has
none). The export stamp is put only on records the caller may export.

**The remote logger** (`--remote-log`, the WSJT-X UDP protocol) is the
station's: it receives the QSOs made under the station call, not a
guest's own.

**What the server can and cannot enforce.** Where the callsign passes
through the server it is checked: an APRS beacon's source call, a packet
frame's and the AX.25 terminal's own call must be the on-air call (any
SSID, `-0` to `-15`), or the command is refused with the reason. (With no
station callsign set there is nothing to hold anyone to, and the check is
off.) When the call on the air changes — a guest on their own call takes
the seat or leaves it — the terminal's sessions made under the other call
are disconnected and the station stops answering connects to it (and
answers to the station call again when that is back on the air). The
unattended APRS beacon keeps the source an admin configured. AX.25
addresses are six letters and digits with no room for a `/`, so a guest on
a call like `F/K1FM` has no packet. In the
audio modes (FT8, JS8, CW, RTTY, SSTV, voice) the callsign is in audio
the browser makes, which the server cannot read: the page follows the
on-air call and does not let a member type another, but a modified page
could send anything. Accountability there is the `OPERATOR` field on
what gets logged and the audit log's `key-down` and `tx` lines, which
name the account behind every transmission a browser makes.
The unattended APRS beacon is the station's and is not in it.

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

`YOU name · role` opens the account menu: SCHEDULE, CHANGE PASSWORD (your
own: the current one, then the new one twice; your sessions stay), STATION
SETTINGS (admins) and LOG OUT. A visitor sees `visitor · LOG IN` there.

Asked for the seat, the holder gets a bar with **HAND OVER** / **DECLINE**.
Unanswered, the request is granted when the time runs out (60 s by
default, 0 = only by hand) and never while the holder is transmitting.
Declined, the asker stays in line for when the seat frees; asking again
within a minute of a decline puts the bar back up, but the time after
which that request is granted unanswered starts only when the minute is
over. When the seat frees, the first in line has 30 s to take it before
it goes to the next.

A logged-in holder whose connection drops (a reload, a phone off the
network) keeps the seat for 30 s; the same browser coming back within
that (or the same login taking the seat in another tab) has it again
without asking, with the time it has held the seat and a request still
unanswered carried over. A visitor's seat frees at once.
So does the seat of a holder the server cuts off: a KICK, a logout, the
account disabled or removed, its password set by an admin, visitors no
longer admitted. Everyone keeps their level: a `control` holder tunes, an
`operate` holder keys.

"Without touching the radio" is the holder's doing: a command that tunes,
sets or keys. What the page sends by itself (a spot for every FT8 decode,
the TX meter it shows) does not count, so a decoder left open keeps
nobody in the seat. When the seat is held longer than the limit while
others wait, the holder is told a minute before it passes on.

### The other doors

The page is how a shared station is operated. These are the other ways in,
and what holds at each:

| Door | Who | Level and seat | TX inhibit, bands | Audit log |
|------|-----|----------------|-------------------|-----------|
| The page (WebSocket) | a session cookie, or a visitor where admitted | both, on every command and TX audio frame | yes | logins, connections, seat, key, `tx` |
| REST with an API token or a cookie | an account, or a visitor where admitted | the level per route; **the radio is read-only**; logging a QSO and the SSTV gallery wait for the seat | n/a: no route transmits | what it changes (policy, log) |
| `rigctld` (off unless `--rigctld-port`) | **anyone who reaches its port** | neither | its key and its CW: yes; the TX time-out: its key | `key-down` / `tx` as `rigctld@127.0.0.1` |
| The unattended APRS beacon | the station (an admin configures it) | n/a | yes | no |
| Packet: a connect to a registered call | whoever is on the air | n/a | yes: not answered | `tx -@- packet link ...` |
| The rig server (`--no-web`) | wfview's own users | not Shared Station at all | no | no |

- **REST.** Every write under `/api/v1/radio/` (tuning, mode, gains, the
  key, the keyer, the tuner, a memory recall) is refused to every account,
  the admin's included (`403`). A shared station is operated from the page,
  by somebody in the seat; a token has no expiry and nobody at it, cannot
  be asked for the seat and is not in the connection list. What a script
  still writes is not the radio: a QSO into the log and a picture into the
  SSTV gallery, both refused (`409`) while a browser of another account
  holds the seat. An admin's token reads and sets the policy, so a script
  can stop the station (TX inhibit, `PUT /api/v1/shared/policy`); it does
  not manage accounts, export them or change a password, which are done
  from a logged-in browser.
- **rigctld** is the station's own software for local programs (WSJT-X,
  fldigi, a logger): it needs no account and is not held to the seat, so
  it tunes and keys whoever sits in it. Leave it on localhost:
  `--rigctld-bind-all` on a shared station hands the radio to the network
  without a login, and the server says so in its log when started that
  way.
- **`--shared` with `--no-web`** does not start: the rig server that
  `--no-web` runs has wfview's own user list and none of this.
- **A private session, or listen-in switched off**, keeps the audio and the
  scope from the others. The radio's status (frequency, mode, meters),
  decoded packet and APRS frames and the packet terminal's text still
  reach every page.

### The seat's rules

The seat's rules are in the Shared station window's **RULES** tab: the seat on or off (off: everyone with
the level controls the radio at once, with no seat at all), idle minutes,
seconds before an unanswered request is granted, the most minutes a seat
may be kept while others wait (0 = no limit), whether a higher level may
take the seat over (then a request from one is granted after 30 s, or
sooner if the ordinary wait is shorter; the holder may still decline),
*Listen-in* (others hear and see while someone has the seat; off, they
get status only) and *Private sessions* (the holder may switch the others'
audio and scope off from the seat menu). Admins always hear and see.
Whoever is kept out sees why for as long as it lasts: the scope is covered
by "W1ABC has started a private session" (or, with listen-in off, "W1ABC
has the seat"), and the strip reads `SEAT W1ABC · private`.
TAKE IT / CLEAR in the **NOW** tab act on a seat someone else holds.

## Access levels

| Level | May |
|-------|-----|
| `listen` | hear the receiver, see the scope and meters, decode locally (CW, FT8, RTTY, SSTV): the FT8 panel opens to decode and leaves the radio alone |
| `control` | take the seat; tune, change mode, filters, gains (RF power, the mic level and COMP among them), scope, recall memories, switch FreeDV and the packet modem on and set their mode |
| `operate` | key the rig: PTT, mic, CW, FT8/JS8/RTTY/SSTV/packet transmit, the tuner; log QSOs, change the ones they logged |
| `admin` | everything, plus accounts, the policy, the connection list, the station callsign, CW macros, memories, rig power, LAN connect, the FreeDV / PSK reporters, the unattended APRS beacon; any QSO in the log, clearing it, importing into it |

A visitor admitted at `control` can therefore take the seat and tune. A
command the server has no level for is `admin`.

A command the page sends that the level lacks is refused on the page with a
red note in the middle of the screen ("Needs control access — log in",
"Needs operate access (you have control)") and, should anything reach the server regardless, refused
there with the same answer. The PTT key is dimmed for anyone who cannot
transmit; the frequency display and MODE for a listener.

## The Shared station window (YOU > STATION SETTINGS, admins)

Four tabs:

- **NOW**: who has the seat (TAKE IT, CLEAR), whether transmitting is
  allowed (INHIBIT TX / ALLOW TX), and who is connected: name or *visitor*,
  level, address, SEAT / ON AIR; KICK ends that connection and frees the
  seat if it had it (the account stays — disable it to keep them out).
- **ACCOUNTS**: the callsign, STATION / OWN (what the station identifies
  as while the account has the seat, see "Callsigns and the log"), level (a
  change applies to the account's open pages at once: lowered below
  `operate` a key it holds is released, below `control` it leaves the
  seat), PASSWORD (sets a new one and ends the account's
  sessions and API tokens), NOTES (a line about the account for the admins only: a name, a
  membership number; the account never sees it), ENABLE / DISABLE, DELETE
  (two presses), and a form to add one. USER LIST at the bottom: EXPORT
  saves every account to `wfweb-users-<date>.json` (name, callsign, level,
  STATION / OWN, notes, disabled, and the password hash — keep the file as
  private as `users.json`); IMPORT adds the accounts of such a file and
  lists what it added and what it skipped. An import never changes an
  account: a name that exists is skipped. A list written by hand may carry
  a plain `password` per entry instead of the hash. A user name is 2 to 32
  letters, digits, `.`, `_` or `-` (no `@`, no `/`: not an e-mail address,
  not `F/K1FM`), unique whatever the case; a password 8 to 256 characters;
  a callsign 3 to 16 letters, digits and `/`, with a letter and a digit.
- **SCHEDULE**: the reservations, a form to add one, and the next two weeks.
- **RULES**: every setting with a line saying what it does; a change
  applies at once. *Visitors without a login*: REFUSED (the login sheet),
  LISTEN or CONTROL — the strip then shows `visitor · LOG IN`, and the sheet
  it opens can be put away; lowering the level applies to visitors already
  connected, refusing them ends their connections. *Connections at once*,
  *TX time-out* (0 = none), *TX inhibit* (the same switch as in NOW),
  *Bands nobody transmits on* (one button per band of the rig; red = closed
  to transmitting for everyone), and the seat's rules.

## Setting it up

```bash
wfweb --shared
```

or `Shared/Enabled=true` in the settings file. On the first start the log
says:

```
Shared: no admin account yet - open the web page and enter setup code K7QX-4MZP
```

and the console status page shows the same code on its Access line
(`shared station - SETUP CODE K7QX-4MZP`).

Open the page, enter the code with a user name and a password of at least
eight characters, and you are the admin. Add the other accounts from
`YOU` > STATION SETTINGS > ACCOUNTS, or over REST (below).

### Files

Everything lives in one directory, `<data dir>/shared` by default
(`~/.local/share/wfweb/wfweb/shared` on Linux, `/data/wfweb/wfweb/shared` in Docker), `--shared-dir <dir>` or
`Shared/Dir=` to move it. The directory is created `0700`, the files `0600`.

| File | Holds |
|------|-------|
| `users.json` | accounts: name, callsign, role, password hash (PBKDF2-HMAC-SHA256, per-user salt, iteration count in the hash), API token hashes |
| `sessions.json` | browser sessions by the SHA-256 of their cookie token (the token itself is never stored), with the address and browser each was opened from |
| `reservations.json` | the reserved slots |
| `audit.log` | one line per event: `2026-11-07T10:11:12Z login clubadmin@192.168.1.20 admin` |

Back up the directory with the rest of the station's data. It holds no
plain-text secret: a copy of it logs nobody in. An unreadable
`sessions.json` is not fatal (everyone logs in again); an unreadable
`reservations.json` is kept aside under another name and the schedule
starts empty; an unreadable `users.json` stops the server. If `audit.log`
cannot be written the server says so in its own log.

### Locked out

```bash
wfweb --shared --shared-reset-admin
```

prints a new setup code. Entering it with an existing user name sets that
account's password, makes it an enabled admin, ends its sessions and its
API tokens and clears its login lockout; a new name adds an admin. The
code is valid for that run only, until it is used.

### Settings

| Setting / flag | Meaning |
|----------------|---------|
| `--shared`, `Shared/Enabled=true` | access control on |
| `--shared-dir <dir>`, `Shared/Dir=` | where accounts, sessions and the audit log live |
| `--shared-reset-admin` | setup code to reset or create an admin (this run only) |
| `--trust-proxy <cidr>[,<cidr>]`, `Shared/TrustProxy=` | reverse proxy address(es) whose `X-Forwarded-For` / `X-Forwarded-Proto` are believed (see *Deployment*). The client is the rightmost `X-Forwarded-For` entry that is not itself a trusted proxy: Cloudflare and nginx append to a list the client may have started, so the first entry is never believed |
| `Shared/Anonymous=none\|listen\|control` | what a visitor without a login gets (default `none`); the RULES tab |
| `Shared/TxInhibit=true` | nobody keys the rig (default off); INHIBIT TX in the NOW tab |
| `Shared/TxInhibitBands=20m, 60m` | bands nobody transmits on (default none), by the rig file's band names; the RULES tab |
| `Shared/TxTimeoutSecs=` | a key held this long is released (default 600, 0 = never, 3600 at most) |
| `Shared/MaxClients=` | connections at once (default 20, 1 to 500); an admin is always let in, and counts once in |
| `Shared/Seat=false` | no seat: everyone with the level controls the radio at once (default on) |
| `Shared/SeatIdleSecs=` | the seat is released after this long without the holder tuning, setting or keying anything (default 600, 0 = never) |
| `Shared/SeatRequestSecs=` | an unanswered request is granted after this (default 60, 0 = only by hand) |
| `Shared/SeatMaxSecs=` | the most a seat is kept while others wait (default 0 = no limit) |
| `Shared/SeatPreemptHigher=true` | a higher level's request is granted after 30 s |
| `Shared/ListenIn=false` | nobody but the holder (and admins) hears or sees while the seat is held |
| `Shared/AllowPrivate=true` | the holder may go private |
| `Shared/PasswordIterations=` | PBKDF2 rounds for new passwords; default 600 000 with libcrypto, 150 000 with Qt's own implementation; 10 000 at least (less is ignored, with a warning) |
| `--cert <file> --key <file>`, `Web/CertFile=`, `Web/KeyFile=` | serve HTTPS with your own certificate (PEM; the certificate file may carry its chain, the key may be RSA or EC, unencrypted). The two go together, and the server does not start on a file it cannot read |

## Deployment

**A shared station lives on a private network.** Its members reach it in
one of two ways:

1. **The local network**: the club's LAN, the station's Wi-Fi.
2. **A private network or VPN** (Tailscale, WireGuard, ZeroTier, the club's
   own VPN) for members away from the station. Nothing is exposed to the
   internet; wfweb's own certificate and accounts are enough. Tailscale's
   `tailscale serve` also gives you a trusted certificate.

**Not supported: a port forward or a public address.** Do not forward
8080 / 8081 from the router to wfweb, and do not run it on a host with a
public address (a VPS, a DMZ) unless a firewall admits nothing but the VPN.
wfweb's HTTP server is small and hand-written, and Shared Station has been
reviewed and fuzzed for a station whose users are already on its network;
it is not built, tested or maintained as a public web service. The
console status page and the log say so when a client comes from a public
address (see *What else changed*).

**`--shared` needs TLS.** The session cookie must not travel in clear, so
wfweb refuses to start in shared mode on plain HTTP unless `--trust-proxy`
names a TLS-terminating proxy in front of it. Logins are refused on the
plain-HTTP REST port (8081) whatever the mode; bearer tokens are accepted
there, so keep that port on the LAN (it exists for microcontrollers there).

**An identity-aware proxy** (Cloudflare Tunnel + Access, Pangolin) is the
same idea from the other side: the proxy refuses everyone it does not know
before a single request reaches wfweb, so it plays the part of the VPN, as
long as its access policy names your members and never "everyone". A
plain reverse proxy facing the internet that passes every visitor on
(nginx, Caddy on their own) is a port forward with extra steps and is not
supported either; on the LAN it is fine. The
proxy terminates TLS with a real certificate, asks for identity, and
forwards to wfweb on `localhost:8080`. Run wfweb with
`--trust-proxy 127.0.0.1` so the audit log and the lockout see the
visitor's address, not the proxy's. Three things the proxy must do: speak
**HTTPS** to wfweb (`https://localhost:8080`: wfweb keeps serving TLS
on its own certificate, so switch the proxy's verification of it off,
`noTLSVerify` in cloudflared, `tls_insecure_skip_verify` in Caddy);
pass the **`Host`** header through as the browser sent it (nginx:
`proxy_set_header Host $host;`), or every WebSocket, and every change
made from the page, is refused with `403` as a cross-site one; and
serve the station over **HTTPS only**: a proxy that says
`X-Forwarded-Proto: http` (the browser came in on port 80) has logins
refused, as on any plain-HTTP hop. Mapping the proxy's identity onto wfweb
accounts is planned; until then users log in to the proxy and then to
wfweb.

Whatever the route, the browser needs a certificate it trusts for the
`Secure` cookie: wfweb's self-signed one works after the warning is
accepted; a real one is better (`tailscale serve`, or `--cert` / `--key`
with a certificate from your own CA or Let's Encrypt via DNS validation,
which needs no open port; restart wfweb after a renewal).

## REST

All under `/api/v1/auth/`; JSON in and out. `POST` bodies are JSON objects.

| Route | Who | Does |
|-------|-----|------|
| `GET /auth/me` | anyone | `{shared, authenticated, user, setupRequired, recovery, secure, anonymousAccess}`; `{shared:false}` without `--shared` |
| `POST /auth/setup` `{code, username, password, callsign}` | first run / recovery, HTTPS only | creates the admin, logs them in (sets the cookie); wrong codes lock the address out like wrong passwords |
| `POST /auth/login` `{username, password}` | anyone, HTTPS only | sets the session cookie; `401` wrong, `403` disabled, `429` + `Retry-After` locked |
| `POST /auth/logout` | session | ends it (and closes its WebSockets), clears the cookie |
| `POST /auth/password` `{current, password}` | session (not a token), HTTPS only | changes the caller's password; this browser stays logged in, the account's other sessions end. `403` wrong current password, `429` after five of them |
| `GET /auth/tokens` | session | the caller's API tokens (ids, names, dates) |
| `POST /auth/tokens` `{name}` | session | `{id, name, token}` — the token is shown this once; twenty an account, the name 64 characters |
| `DELETE /auth/tokens/<id>` | session | revokes it |

Account management under `/api/v1/shared/users`: admins only, and from a
logged-in browser session only. An admin's API token is answered `403
{"error": "manage accounts from a logged-in browser"}` on every one of
these routes: a token has no expiry and outlives a password change, and
these are the routes that make credentials and hand out password hashes.

| Route | Does |
|-------|------|
| `GET` | `{users:[{id, name, callsign, onAir, role, notes, disabled, created, lastLogin, tokens}]}` |
| `POST` `{username, password, role, callsign, notes?}` | adds an account (`201`); `400` a bad name, password, role or callsign, `409` a name that is taken |
| `PUT /<id>` `{role?, callsign?, onAir?, notes?, disabled?, password?, kick?}` | changes it. Everything is checked before anything is changed: one bad field (`400`, a value of the wrong type included: `disabled` and `kick` are `true` / `false`) leaves the others unapplied. `notes` is free text for the admins (500 characters, never shown to the account, and the audit log records that it changed, not what it says); `disabled`, a new `password` and `kick` end the account's sessions, a new `password` its API tokens too. `onAir` is `station` or `own` (`400` without a callsign on the account); a callsign is letters, digits and `/`. `409` when it would leave the station without an admin. The account's open pages follow at once, a new `role` included (a key or a seat the new level does not cover is given up), and so does the on-air call when it holds the seat |
| `DELETE /<id>` | removes it; not yourself, not the last admin (`409`) |
| `GET /export` | `{wfwebUsers: 1, exported, users:[{username, callsign, role, onAir, notes, disabled, passwordHash}]}`: every account, with the hash its password is stored as. No ids, sessions or API tokens |
| `POST /import` `{users:[...]}` | adds each entry (those of an export, or `{username, role, password, callsign?, onAir?, notes?, disabled?}` with a plain password, which is hashed here); at most 500. Answers `{added:[names], skipped:[{username, reason}], users}`. Never replaces an account: an existing name, a bad role or callsign, a missing or too short password are skipped with the reason |

Policy and connections, admins only (a session or an admin's token):

| Route | Does |
|-------|------|
| `GET /api/v1/shared/policy` | `{policy:{anonymous, txInhibit, txInhibitBands, txTimeoutSecs, maxClients, ...}}` |
| `PUT /api/v1/shared/policy` `{anonymous?, txInhibit?, txInhibitBands?, txTimeoutSecs?, maxClients?, seat?, seatIdleSecs?, seatRequestSecs?, seatMaxSecs?, seatPreemptHigher?, listenIn?, allowPrivate?}` | `txInhibitBands` is the whole list of band names (`["20m","60m"]`, `[]` for none). Changes what is given; applied at once, saved to the settings file, pushed to every page as `{type:"sharedPolicy", policy}`. The switches are JSON `true` / `false` and the numbers whole numbers: anything else (`"true"`, `"600"`) is `400`, not read as off or zero |
| `GET /api/v1/shared/connections` | `{connections:[{id, name, anonymous, role, callsign, ip, since, keying, seat}], max}` |
| `DELETE /api/v1/shared/connections/<id>` | ends that connection and frees the seat if it had it (not your own: `409`) |
| `GET /api/v1/shared/seat` | `{seat:{enabled, holder:{name, id, role, since, private, keying}|null, queue, grace, privateNow, listenIn, allowPrivate, reservation?, next?}}` (anyone the station admits: it is what the strip shows; a visitor gets the slot's name and times, not its account, callsign or title) |
| `DELETE /api/v1/shared/seat` | frees the seat (admin) |
| `GET /api/v1/shared/reservations` | `{reservations:[{id, userId, name, callsign, title, start, minutes, repeat, until?, createdBy, created}], upcoming:[runs in the next 14 days: {id, name, title, start, end, minutes, repeat}], active}` (any account; `401` for a visitor without one) |
| `POST /api/v1/shared/reservations` `{username|userId, start (ISO), minutes, repeat: once|daily|weekly, until?, title?}` | adds a slot (`201`); `400` bad input (a listener or a disabled account, a length that is not a whole number of minutes from 5 to 1440, a one-off slot already over), `409 {conflict}` when it overlaps another. A `start` or `until` without a zone is UTC |
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
is `listen`, a write under `/radio/` is `403` for everyone (the radio is
read-only over REST), a write to the SSTV gallery is `control`,
the logbook is `operate`
(clearing it and importing into it: `admin`), `/station` and `/cw/macros`
are `admin`. Short of it: `403 {"error": "needs operate access", "need":
"operate"}`. Where visitors are admitted, a request with no cookie and no
token is answered at the visitor's level. A path is the same route with or
without a trailing slash. `GET /radio/info` gives the path of the logbook
file to an admin only.

Three more things hold for every request of a shared station:

- **A write made with the session cookie must come from the station's own
  page.** A browser names the page a request comes from in `Origin`; one
  from another host is `403 {"error": "cross-site request refused"}`
  (`SameSite=Strict` keeps other sites out, but not a page on a sibling
  host of the same domain). A script with a token, or with no `Origin`, is
  not concerned.
- **Who is asking is read from the headers, before the body.** A request
  that will be refused is refused without its body being taken, and a body
  is capped by its route: 64 KB, except the ADIF import (16 MB), the rest
  of `/logbook` (4 MB), an SSTV picture (6 MB) and a user-list import
  (2 MB). Over that: `413`.
- **Password checks run two at a time.** They are the costly part of a
  login (on purpose); the rest wait in a short line, and when it is full
  the answer is `503` with `Retry-After`.

The WebSocket says who the browser is as its first message:
`{type:"access", shared, user:{id, name, callsign, onAir, role}|null,
anonymous, role, can:{control, tx, admin}, denied:{cmd: level},
needsSeat:{cmd: true}, policy}` (just
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
  is dropped, as is one whose upload stops arriving for 30 s or that stops
  reading its response for 60 s; no more than 128 HTTP connections are kept
  at once. A slow but moving upload or download (a log import, the 27 MB CW
  model on a weak link) is left alone.
- A WebSocket upgrade whose `Origin` names another host than the one it was
  sent to is refused with `403` (a page on another site cannot drive your
  radio with your browser's cookie). Scripts, which send no `Origin`, are
  unaffected. An upgrade request whose headers do not fit in 16 KB is
  refused (`431`).
- A debug log (`-d`) no longer carries the `Cookie` and `Authorization`
  headers of the requests it lists: they are a session and an API token,
  and logs get attached to bug reports.
- What a QSO's short fields hold (call, band, mode, reports, grids) is
  cleaned of markup characters and control codes as it is stored, and an
  AX.25 terminal call must be one (six letters and digits, SSID 0 to 15).
- Responses carry `X-Content-Type-Options: nosniff` and `Referrer-Policy:
  no-referrer`; with `--cert/--key`, `Strict-Transport-Security` too (never
  with the self-signed certificate, which would lock the browser out).
- The console status page states the access mode: *open - anyone who
  reaches this port can transmit* without `--shared`.
- The server says when the internet can reach it, open or shared: a client
  from a public address (anything but loopback, the private ranges,
  link-local, 100.64/10 as Tailscale uses, IPv6 unique-local, or the host's
  own IPv6 prefix) puts an `Internet  PUBLIC CLIENT <address>` line on the
  console status page and a `Network:` warning in the log (the first ten
  addresses; the page keeps the count), and so does a public IPv4 address
  on one of the host's interfaces (a VPS). A client handed on by a
  `--trust-proxy` proxy is not counted. It is a warning only: nothing is
  refused.

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

Personal data: user names, callsigns and addresses in `audit.log`, and the
addresses and browser names in `sessions.json`, are personal data under
GDPR and its relatives. Keep them where only the station's keeper reads
them, and prune the log when it is no longer needed.

### The audit log's events

One line each: the time in UTC, the event, `who@address`, and a detail.

| Events | When |
|--------|------|
| `setup`, `setup-reset`, `setup-failed`, `recovery-enabled` | the first admin, a recovery, a wrong setup code, `--shared-reset-admin` |
| `login`, `login-failed`, `login-locked`, `login-disabled`, `logout` | logins; a name no account could have is written as `?` |
| `password-changed`, `password-change-failed` | one's own password |
| `token-created`, `token-revoked` | API tokens |
| `user-created`, `user-updated`, `user-removed`, `users-export`, `users-import` | accounts (what changed, never a password or what a note says) |
| `policy`, `kick` | the rules, a connection ended by an admin |
| `connect`, `disconnect` | a page's WebSocket |
| `seat-take`, `seat-release`, `seat-request`, `seat-declined`, `seat-grace`, `seat-private`, `seat-reserved` | the seat |
| `reservation-add`, `reservation-remove` | the schedule |
| `key-down`, `key-up`, `unkey`, `tx` | the transmitter: a key with its frequency, mode and duration, a key the server released and why, and what went out without a key of its own |
| `onair` | the call on the air changed |
| `qso-edit`, `qso-delete`, `log-import`, `log-clear`, `log-merge` | the log |
