"""Shared Station, phase 6: callsigns on the air and the QSO log.

Its own --shared wfweb on a mock radio, with a UDP socket standing in for
the WSJT-X logger (--remote-log).  The station call is W1CLUB.  Accounts:

- clubadmin  admin
- mem        operate, on the station call, own callsign W1MEM (a club member)
- guest      operate, on their OWN call F/K1FM (a licensed guest)
- lis        listen
- nocall     operate, set to its own call and then left without a callsign

Then, in order (the tests share the log they build):

- the account's onAir field over REST, and what rigInfo says on connect;
- a member logging from a socket: STATION_CALLSIGN / OPERATOR / the account
  are the server's, whatever the client sent; everyone with an account
  gets the broadcast;
- the guest taking the seat: stationChanged for everyone, the QSOs stamped
  with the guest's call from the socket and from a REST token, the
  broadcast only to the guest and the admin; the release puts the station
  call back;
- REST stamping with the seat free (on the station call, for the guest too);
- who sees what: the page, the single record, the ADIF document, visitors;
- /worked per callsign;
- edits and deletes: one's own only, admins any, ownerless records
  admin-only, the three stamped fields never reassigned;
- clear / import / mergeLogbook: admins only;
- export per callsign, and the export stamp only on one's own records;
- an own-call account without a callsign is refused the seat;
- termRegister / aprsTxBeacon held to the on-air call, any SSID;
- an account edit and the seat policy move the on-air call;
- the remote logger got the club QSOs and none of the guest's;
- the audit lines.
"""

from __future__ import annotations

import json
import socket
import threading
import time

import pytest

from conftest import launch_shared_wfweb, set_cookie, _run_on_loop, _stop_wfweb
from mock_radio import MockIcomRadio

CLUB = "W1CLUB"
GUEST_CALL = "F/K1FM"
PASSWORDS = {"clubadmin": "club-admin-pw-2026", "mem": "member-pw-one", "guest": "guest-pw-number-1",
             "lis": "listener-pw-1", "nocall": "nocall-pw-one-1"}
ACCOUNTS = {"mem": ("operate", "W1MEM"), "guest": ("operate", GUEST_CALL), "lis": ("listen", ""),
            "nocall": ("operate", "K9NC")}


class UdpSink:
    """The 'WSJT-X logger': every datagram the server sends, kept."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.2)
        self.port = self.sock.getsockname()[1]
        self.datagrams = []
        self._stop = False
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop:
            try:
                data, _ = self.sock.recvfrom(65535)
                self.datagrams.append(data)
            except socket.timeout:
                pass
            except OSError:
                break

    def saw(self, needle: bytes, timeout=3.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if any(needle in d for d in list(self.datagrams)):
                return True
            time.sleep(0.1)
        return False

    def close(self):
        self._stop = True
        self._thread.join(timeout=2)
        self.sock.close()


@pytest.fixture(scope="module")
def log_mock(event_loop):
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


@pytest.fixture(scope="module")
def srv(log_mock, event_loop, tmp_path_factory):
    home = tmp_path_factory.mktemp("sharedlog-home")
    sink = UdpSink()
    s = launch_shared_wfweb(log_mock, event_loop, home,
                            extra_args=("--logbook", str(home / "logbook.adi"),
                                        "--remote-log", f"127.0.0.1:{sink.port}"))
    s.sink = sink
    s.logbook_path = home / "logbook.adi"
    r = s.post("/api/v1/auth/setup", {"code": s.setup_code(), "username": "clubadmin",
                                       "password": PASSWORDS["clubadmin"], "callsign": "K1ADM"})
    assert r.status_code == 200, r.text
    s.admin_cookie, _ = set_cookie(r)
    s.cookies = {"clubadmin": s.admin_cookie}
    s.tokens = {}
    s.ids = {"clubadmin": r.json()["user"]["id"]}
    for name, (role, call) in ACCOUNTS.items():
        r = s.post("/api/v1/shared/users", {"username": name, "password": PASSWORDS[name], "role": role,
                                            "callsign": call}, cookie=s.admin_cookie)
        assert r.status_code == 201, r.text
        s.ids[name] = r.json()["user"]["id"]
        r = s.post("/api/v1/auth/login", {"username": name, "password": PASSWORDS[name]})
        assert r.status_code == 200, r.text
        s.cookies[name], _ = set_cookie(r)
    for name in ("clubadmin", "mem", "guest", "lis"):
        r = s.post("/api/v1/auth/tokens", {"name": "t"}, cookie=s.cookies[name])
        assert r.status_code == 201, r.text
        s.tokens[name] = r.json()["token"]
    r = s.req("PUT", "/api/v1/station", json={"callsign": CLUB, "grid": "FN31"}, cookie=s.admin_cookie)
    assert r.status_code == 200, r.text
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        j = s.get("/api/v1/radio/info", cookie=s.admin_cookie).json()
        if (j.get("info", j)).get("connected"):
            break
        time.sleep(0.3)
    else:
        pytest.fail("rig never connected")
    yield s
    _stop_wfweb(s.proc)
    sink.close()


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------


def recv_until(ws, pred, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            msg = json.loads(ws.recv(timeout=max(0.05, deadline - time.monotonic())))
        except TimeoutError:
            return None
        if isinstance(msg, dict) and pred(msg):
            return msg
    return None


def of_type(ws, kind, pred=lambda m: True, timeout=5.0):
    return recv_until(ws, lambda m: m.get("type") == kind and pred(m), timeout)


def drain(ws, seconds=0.3):
    out = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            msg = json.loads(ws.recv(timeout=max(0.05, deadline - time.monotonic())))
            if isinstance(msg, dict):
                out.append(msg)
        except TimeoutError:
            break
    return out


def send(ws, cmd, **fields):
    fields["cmd"] = cmd
    ws.send(json.dumps(fields))


def denied_for(ws, cmd, timeout=0.8, **fields):
    send(ws, cmd, **fields)
    return recv_until(ws, lambda m: m.get("type") == "denied" and m.get("cmd") == cmd, timeout)


def open_ws(srv, name):
    """A logged-in socket, past everything the server says on connect.
    Returns (ws, {type: first message of that type})."""
    ws = srv.ws(cookie=srv.cookies[name])
    first = {}
    for m in drain(ws, 1.5):
        first.setdefault(m.get("type"), m)
    assert first.get("access", {}).get("shared") is True, f"no access message for {name}: {list(first)}"
    assert "seat" in first and "rigInfo" in first and "logbook" in first, list(first)
    return ws, first


def take_seat(ws):
    send(ws, "seatTake")
    seat = of_type(ws, "seat", lambda m: m.get("mine"))
    assert seat is not None, "the seat was not given"
    return seat


def release_seat(ws):
    send(ws, "seatRelease")
    assert of_type(ws, "seat", lambda m: not m.get("mine") and not m.get("holder")) is not None


def qso(call, hhmmss, **extra):
    q = {"call": call, "date": "20261113", "time": hhmmss, "freq": 14074000, "band": "20M", "mode": "FT8",
         "rstSent": "-10", "rstRcvd": "-12"}
    q.update(extra)
    return q


def rest(srv, method, path, name, body=None, **kw):
    if body is not None:
        kw["json"] = body
    return srv.req(method, path, token=srv.tokens[name], plain=True, **kw)


def entries(srv, name, **params):
    r = rest(srv, "GET", "/api/v1/logbook", name, params=params or None)
    assert r.status_code == 200, r.text
    return r.json()


def calls_seen(srv, name):
    return sorted(e["call"] for e in entries(srv, name, limit=100)["entries"])


def by_call(srv, call):
    for e in entries(srv, "clubadmin", limit=100)["entries"]:
        if e["call"] == call:
            return e
    raise AssertionError(f"{call} is not in the log")


def set_policy(srv, **fields):
    r = srv.req("PUT", "/api/v1/shared/policy", json=fields, cookie=srv.admin_cookie)
    assert r.status_code == 200, r.text
    return r.json()["policy"]


def put_user(srv, name, **fields):
    return srv.req("PUT", f"/api/v1/shared/users/{srv.ids[name]}", json=fields, cookie=srv.admin_cookie)


# ---------------------------------------------------------------------------
# accounts and what a browser is told on connect
# ---------------------------------------------------------------------------


def test_on_air_field_and_station_info(srv):
    users = {u["name"]: u for u in srv.get("/api/v1/shared/users", cookie=srv.admin_cookie).json()["users"]}
    assert all(u["onAir"] == "station" for u in users.values()), "the default is the station call"

    r = put_user(srv, "guest", onAir="own")
    assert r.status_code == 200 and r.json()["user"]["onAir"] == "own", r.text
    r = put_user(srv, "lis", onAir="own")
    assert r.status_code == 400 and "callsign" in r.json()["error"], "own needs a callsign on the account"
    assert put_user(srv, "mem", onAir="sometimes").status_code == 400
    assert put_user(srv, "mem", callsign="not a call!").status_code == 400
    # nocall: set to its own call, then the callsign is withdrawn.
    assert put_user(srv, "nocall", onAir="own").status_code == 200
    assert put_user(srv, "nocall", callsign="").status_code == 200
    # Not an admin's own business only: a member cannot touch accounts at all.
    assert srv.req("PUT", f"/api/v1/shared/users/{srv.ids['guest']}", json={"onAir": "station"},
                   cookie=srv.cookies["guest"]).status_code == 403

    st = srv.get("/api/v1/station", cookie=srv.admin_cookie).json()
    assert st["callsign"] == CLUB and st["onAir"] == CLUB and st["onAirOwn"] is False, st

    ws, first = open_ws(srv, "guest")
    try:
        info = first["rigInfo"]
        assert info["stationCallsign"] == CLUB and info["clubCallsign"] == CLUB and info["onAirOwn"] is False, info
        assert first["access"]["user"]["onAir"] == "own" and first["access"]["user"]["callsign"] == GUEST_CALL
        assert first["access"]["denied"].get("mergeLogbook") == "admin", "the browser-log migration is refused locally"
        assert first["logbook"]["count"] == 0
    finally:
        ws.close()


# ---------------------------------------------------------------------------
# stamping and the broadcast
# ---------------------------------------------------------------------------


def test_member_logs_on_the_station_call(srv):
    socks = {n: open_ws(srv, n)[0] for n in ("clubadmin", "mem", "guest", "lis")}
    try:
        take_seat(socks["mem"])
        for w in socks.values():
            drain(w)
        # What the client claims is not what is recorded.
        send(socks["mem"], "qsoLogged", qso=qso("DX1AA", "100000", stationCall="EVIL1", operator="EVIL2",
                                                userId=srv.ids["guest"]))
        for name, w in socks.items():
            added = of_type(w, "qsoAdded")
            assert added is not None, f"{name} sees a QSO on the station call"
            q = added["qso"]
            assert q["stationCall"] == CLUB and q["operator"] == "W1MEM", q
            assert q["userId"] == srv.ids["mem"] and q["operatorName"] == "mem", q
            assert added["count"] == 1, f"{name}: {added}"
        release_seat(socks["mem"])
    finally:
        for w in socks.values():
            w.close()
    assert srv.sink.saw(b"DX1AA"), "a club QSO goes to the remote logger"
    disk = srv.logbook_path.read_bytes()
    assert b"<STATION_CALLSIGN:6>W1CLUB" in disk and b"<OPERATOR:5>W1MEM" in disk and b"<APP_WFWEB_USER:" in disk


def test_guest_on_the_seat_is_on_the_air_under_their_own_call(srv):
    socks = {n: open_ws(srv, n)[0] for n in ("clubadmin", "mem", "guest", "lis")}
    try:
        for w in socks.values():
            drain(w)
        take_seat(socks["guest"])
        for name, w in socks.items():
            st = of_type(w, "stationChanged")
            assert st is not None, f"{name} is told the on-air call"
            assert st["callsign"] == GUEST_CALL and st["clubCallsign"] == CLUB, st
            assert st["onAirOwn"] is True and st["onAirOperator"] == "guest", st
        # The configured station call did not move.
        station = srv.get("/api/v1/station", cookie=srv.admin_cookie).json()
        assert station["callsign"] == CLUB and station["onAir"] == GUEST_CALL and station["onAirOwn"] is True

        for w in socks.values():
            drain(w)
        send(socks["guest"], "qsoLogged", qso=qso("DX2BB", "100100", stationCall=CLUB, operator="W1MEM"))
        for name in ("guest", "clubadmin"):
            added = of_type(socks[name], "qsoAdded")
            assert added is not None, f"{name} sees the guest's QSO"
            q = added["qso"]
            assert q["stationCall"] == GUEST_CALL and q["operator"] == GUEST_CALL and q["userId"] == srv.ids["guest"], q
            assert added["count"] == 2
        for name in ("mem", "lis"):
            assert of_type(socks[name], "qsoAdded", timeout=1.0) is None, f"{name} must not see the guest's own-call QSO"

        # A script of the same account, while their browser has the seat.
        r = rest(srv, "POST", "/api/v1/logbook", "guest", qso("DX5EE", "100200", stationCall=CLUB))
        assert r.status_code == 202, r.text
        assert r.json()["stationCall"] == GUEST_CALL and r.json()["operator"] == GUEST_CALL
        # Somebody else's script cannot log while the guest has the seat.
        assert rest(srv, "POST", "/api/v1/logbook", "mem", qso("DX9ZZ", "100300")).status_code == 409

        for w in socks.values():
            drain(w)
        release_seat(socks["guest"])
        for name, w in socks.items():
            st = of_type(w, "stationChanged")
            assert st is not None and st["callsign"] == CLUB and st["onAirOwn"] is False, f"{name}: {st}"
    finally:
        for w in socks.values():
            w.close()


def test_rest_stamping_with_the_seat_free(srv):
    # Nobody on the seat: the station is on its own call, for the guest too.
    r = rest(srv, "POST", "/api/v1/logbook", "guest", qso("DX3CC", "100400", stationCall=GUEST_CALL, operator="EVIL"))
    assert r.status_code == 202, r.text
    q = r.json()
    assert q["stationCall"] == CLUB and q["operator"] == GUEST_CALL and q["userId"] == srv.ids["guest"], q
    r = rest(srv, "POST", "/api/v1/logbook", "mem", qso("DX4DD", "100500"))
    assert r.status_code == 202, r.text
    q = r.json()
    assert q["stationCall"] == CLUB and q["operator"] == "W1MEM" and q["operatorName"] == "mem", q
    assert rest(srv, "POST", "/api/v1/logbook", "lis", qso("DX9ZZ", "100600")).status_code == 403


# ---------------------------------------------------------------------------
# who sees what
# ---------------------------------------------------------------------------

CLUB_QSOS = ["DX1AA", "DX3CC", "DX4DD"]
OWN_QSOS = ["DX2BB", "DX5EE"]


def test_visibility(srv):
    assert calls_seen(srv, "clubadmin") == sorted(CLUB_QSOS + OWN_QSOS)
    assert calls_seen(srv, "guest") == sorted(CLUB_QSOS + OWN_QSOS), "the guest sees the club log and their own"
    assert calls_seen(srv, "mem") == CLUB_QSOS
    assert calls_seen(srv, "lis") == CLUB_QSOS
    assert entries(srv, "clubadmin")["total"] == 5 and entries(srv, "guest")["total"] == 5
    assert entries(srv, "mem")["total"] == 3 and entries(srv, "lis")["total"] == 3

    # Paging walks the visible set and ends without an empty page.
    seen, cursor = [], None
    for _ in range(6):
        page = entries(srv, "mem", limit=1, **({"before": cursor} if cursor else {}))
        seen += [e["call"] for e in page["entries"]]
        cursor = page.get("next")
        if not cursor:
            break
    assert seen == ["DX4DD", "DX3CC", "DX1AA"], seen

    own = by_call(srv, "DX2BB")
    assert rest(srv, "GET", f"/api/v1/logbook/{own['id']}", "mem").status_code == 404
    assert rest(srv, "GET", f"/api/v1/logbook/{own['id']}", "guest").json()["call"] == "DX2BB"
    assert entries(srv, "mem", call="DX2BB")["entries"] == []

    adif_mem = rest(srv, "GET", "/api/v1/logbook/adif", "mem").text
    assert "DX1AA" in adif_mem and "DX2BB" not in adif_mem and "DX5EE" not in adif_mem
    adif_admin = rest(srv, "GET", "/api/v1/logbook/adif", "clubadmin").text
    assert "DX2BB" in adif_admin and "<OPERATOR:6>F/K1FM" in adif_admin

    # A socket's summary carries the viewer's own count.
    for name, count in (("mem", 3), ("guest", 5), ("lis", 3), ("clubadmin", 5)):
        ws, first = open_ws(srv, name)
        ws.close()
        assert first["logbook"]["count"] == count, f"{name}: {first['logbook']}"

    # Visitors, when the policy lets them in at all, see no log.
    set_policy(srv, anonymous="listen")
    try:
        r = srv.get("/api/v1/logbook", plain=True)
        assert r.status_code == 200 and r.json()["entries"] == [] and r.json()["total"] == 0, r.text
        assert "DX1AA" not in srv.get("/api/v1/logbook/adif", plain=True).text
        assert srv.get("/api/v1/logbook/worked", plain=True).json()["calls"] == {}
        assert srv.get("/api/v1/logbook/export", plain=True).json()["count"] == 0
    finally:
        set_policy(srv, anonymous="none")


def test_worked_is_per_callsign(srv):
    w = rest(srv, "GET", "/api/v1/logbook/worked", "guest").json()
    assert sorted(w["calls"]) == OWN_QSOS and w["callsign"] == GUEST_CALL and w["total"] == 2, w
    w = rest(srv, "GET", "/api/v1/logbook/worked", "mem").json()
    assert sorted(w["calls"]) == CLUB_QSOS and w["callsign"] == CLUB, w
    assert sorted(rest(srv, "GET", "/api/v1/logbook/worked", "lis").json()["calls"]) == CLUB_QSOS
    assert sorted(rest(srv, "GET", "/api/v1/logbook/worked", "clubadmin").json()["calls"]) == CLUB_QSOS


# ---------------------------------------------------------------------------
# changes
# ---------------------------------------------------------------------------


def test_edit_and_delete_rules(srv):
    mine = by_call(srv, "DX4DD")           # mem's, club call
    guests_club = by_call(srv, "DX3CC")    # guest's, club call
    guests_own = by_call(srv, "DX2BB")     # guest's, own call

    # REST: one's own only.
    r = rest(srv, "PUT", f"/api/v1/logbook/{guests_club['id']}", "mem", dict(guests_club, comment="mine now"))
    assert r.status_code == 403, r.text
    assert rest(srv, "DELETE", f"/api/v1/logbook/{guests_club['id']}", "mem").status_code == 403
    assert rest(srv, "PUT", f"/api/v1/logbook/{guests_own['id']}", "mem", dict(guests_own)).status_code == 404
    assert rest(srv, "PUT", f"/api/v1/logbook/{mine['id']}", "lis", dict(mine)).status_code == 403

    # An edit keeps who made the QSO and under which call, whatever it sends.
    r = rest(srv, "PUT", f"/api/v1/logbook/{mine['id']}", "mem",
             dict(mine, comment="corrected", stationCall="EVIL1", operator="EVIL2", userId=srv.ids["guest"]))
    assert r.status_code == 200, r.text
    q = r.json()
    assert q["comment"] == "corrected" and q["stationCall"] == CLUB and q["operator"] == "W1MEM", q
    assert q["userId"] == srv.ids["mem"]

    # An admin changes any record, and cannot reassign it either.
    r = rest(srv, "PUT", f"/api/v1/logbook/{guests_own['id']}", "clubadmin",
             dict(guests_own, comment="fixed by the admin", stationCall=CLUB, operator="K1ADM"))
    assert r.status_code == 200, r.text
    q = r.json()
    assert q["stationCall"] == GUEST_CALL and q["operator"] == GUEST_CALL and q["userId"] == srv.ids["guest"], q

    # Sockets: no seat needed to correct one's own record; others get 'denied'.
    mem, _ = open_ws(srv, "mem")
    guest, _ = open_ws(srv, "guest")
    lis, _ = open_ws(srv, "lis")
    try:
        d = denied_for(mem, "updateQso", id=guests_club["id"], qso=dict(guests_club, comment="x"))
        assert d is not None and d["need"] == "owner", d
        assert denied_for(mem, "deleteQso", id=guests_own["id"]) is not None, "a record one cannot see is not deletable"
        assert denied_for(lis, "deleteQso", id=mine["id"])["need"] == "operate"
        for w in (mem, guest, lis):
            drain(w)
        send(mem, "updateQso", id=mine["id"], qso=dict(mine, comment="from the socket", stationCall="EVIL1"))
        for w in (mem, guest, lis):
            up = of_type(w, "qsoUpdated")
            assert up is not None and up["qso"]["comment"] == "from the socket" and up["qso"]["stationCall"] == CLUB, up

        # The guest edits their own-call QSO: the member's socket hears nothing.
        for w in (mem, guest):
            drain(w)
        send(guest, "updateQso", id=guests_own["id"], qso=dict(guests_own, comment="guest edit"))
        assert of_type(guest, "qsoUpdated")["qso"]["comment"] == "guest edit"
        assert of_type(mem, "qsoUpdated", timeout=1.0) is None

        # Delete: the guest removes one of their own-call QSOs.
        gone = by_call(srv, "DX5EE")
        for w in (mem, guest):
            drain(w)
        send(guest, "deleteQso", id=gone["id"])
        dele = of_type(guest, "qsoDeleted")
        assert dele is not None and dele["id"] == gone["id"] and dele["count"] == 4, dele
        assert of_type(mem, "qsoDeleted", timeout=1.0) is None
    finally:
        for w in (mem, guest, lis):
            w.close()
    assert calls_seen(srv, "clubadmin") == sorted(CLUB_QSOS + ["DX2BB"])


def test_whole_log_operations_are_admin_only(srv):
    adif = b"<CALL:5>OLD1A <QSO_DATE:8>20200101 <TIME_ON:6>120000 <BAND:3>40M <MODE:2>CW <EOR>\n"
    for name in ("mem", "guest"):
        r = rest(srv, "DELETE", "/api/v1/logbook", name)
        assert r.status_code == 403 and r.json()["need"] == "admin", r.text
        r = srv.req("POST", "/api/v1/logbook/adif", token=srv.tokens[name], plain=True, data=adif)
        assert r.status_code == 403 and r.json()["need"] == "admin", r.text
    mem, _ = open_ws(srv, "mem")
    try:
        d = denied_for(mem, "mergeLogbook", entries=[qso("OLD2B", "090000")])
        assert d is not None and d["need"] == "admin", d
    finally:
        mem.close()
    assert "OLD2B" not in calls_seen(srv, "clubadmin")

    r = srv.req("POST", "/api/v1/logbook/adif", token=srv.tokens["clubadmin"], plain=True, data=adif)
    assert r.status_code == 202 and r.json()["added"] == 1, r.text
    # Imported: no account behind it.  Everyone sees it (no station call =
    # the station's), only an admin changes it.
    old = by_call(srv, "OLD1A")
    assert "userId" not in old and "operator" not in old
    assert "OLD1A" in calls_seen(srv, "mem")
    assert rest(srv, "PUT", f"/api/v1/logbook/{old['id']}", "mem", dict(old, comment="x")).status_code == 403
    assert rest(srv, "DELETE", f"/api/v1/logbook/{old['id']}", "mem").status_code == 403
    assert rest(srv, "PUT", f"/api/v1/logbook/{old['id']}", "clubadmin", dict(old, comment="ok")).status_code == 200
    assert rest(srv, "DELETE", f"/api/v1/logbook/{old['id']}", "clubadmin").status_code == 202


def test_export_is_per_callsign(srv):
    own = by_call(srv, "DX2BB")
    club_ids = sorted(by_call(srv, c)["id"] for c in CLUB_QSOS)

    g = rest(srv, "GET", "/api/v1/logbook/export", "guest").json()
    assert g["ids"] == [own["id"]], "a member exports the QSOs made on their own call"
    assert "DX2BB" in g["adif"] and "DX1AA" not in g["adif"] and "APP_WFWEB" not in g["adif"]
    assert "<OPERATOR:6>F/K1FM" in g["adif"] and "<STATION_CALLSIGN:6>F/K1FM" in g["adif"]

    a = rest(srv, "GET", "/api/v1/logbook/export", "clubadmin").json()
    assert sorted(a["ids"]) == club_ids, "an admin exports the club log"
    assert "DX2BB" not in a["adif"]
    assert rest(srv, "GET", "/api/v1/logbook/export", "clubadmin", params={"own": "1"}).json()["count"] == 0
    assert rest(srv, "GET", "/api/v1/logbook/export", "mem").json()["count"] == 0, "a member on the club call has none of their own"
    assert "DX" not in rest(srv, "GET", "/api/v1/logbook/adif", "mem", params={"new": "1"}).text

    # The stamp only lands on records the caller may export.
    r = rest(srv, "POST", "/api/v1/logbook/exported", "mem", {"ids": [own["id"]] + club_ids})
    assert r.status_code == 200 and r.json()["marked"] == 0, r.text
    r = rest(srv, "POST", "/api/v1/logbook/exported", "guest", {"ids": [own["id"]] + club_ids})
    assert r.status_code == 200 and r.json()["marked"] == 1 and r.json()["unexported"] == 0, r.text
    assert "exported" in by_call(srv, "DX2BB") and "exported" not in by_call(srv, "DX1AA")
    assert entries(srv, "clubadmin")["unexported"] == 3 and entries(srv, "guest")["unexported"] == 0


# ---------------------------------------------------------------------------
# the seat, and the calls the server sees
# ---------------------------------------------------------------------------


def test_own_call_account_without_a_callsign_is_refused_the_seat(srv):
    ws, first = open_ws(srv, "nocall")
    try:
        assert first["access"]["user"]["onAir"] == "own" and first["access"]["user"]["callsign"] == ""
        d = denied_for(ws, "seatTake")
        assert d is not None and d["need"] == "callsign" and "callsign" in d["reason"], d
        assert srv.get("/api/v1/shared/seat", cookie=srv.admin_cookie).json()["seat"].get("holder") is None
    finally:
        ws.close()


def test_packet_calls_must_be_the_on_air_call(srv):
    guest, _ = open_ws(srv, "guest")
    try:
        take_seat(guest)
        d = denied_for(guest, "termRegister", ownCall=CLUB, chan=0)
        assert d is not None and d["need"] == "callsign" and GUEST_CALL in d["reason"], d
        assert denied_for(guest, "termConnect", ownCall="W1MEM-1", peerCall="N0PEER", chan=0) is not None
        assert denied_for(guest, "aprsTxBeacon", src=CLUB + "-9", lat=42.0, lon=-71.0) is not None
        assert denied_for(guest, "termRegister", ownCall=GUEST_CALL + "-16", chan=0) is not None, "16 is no SSID"
        assert denied_for(guest, "termRegister", ownCall=GUEST_CALL + "-7", chan=0) is None
        assert denied_for(guest, "termRegister", ownCall=GUEST_CALL, chan=0) is None
        assert denied_for(guest, "aprsTxBeacon", src=GUEST_CALL + "-9", lat=42.0, lon=-71.0) is None
        send(guest, "termUnregister", ownCall=GUEST_CALL + "-7", chan=0)
        send(guest, "termUnregister", ownCall=GUEST_CALL, chan=0)
        release_seat(guest)
    finally:
        guest.close()
    mem, _ = open_ws(srv, "mem")
    try:
        take_seat(mem)
        assert denied_for(mem, "termRegister", ownCall="W1MEM", chan=0) is not None, "a member is on the station call"
        assert denied_for(mem, "termRegister", ownCall=CLUB + "-1", chan=0) is None
        send(mem, "termUnregister", ownCall=CLUB + "-1", chan=0)
        release_seat(mem)
    finally:
        mem.close()


def test_account_edits_and_the_seat_policy_move_the_on_air_call(srv):
    guest, _ = open_ws(srv, "guest")
    watcher, _ = open_ws(srv, "lis")
    try:
        take_seat(guest)
        assert of_type(watcher, "stationChanged", lambda m: m["callsign"] == GUEST_CALL) is not None

        assert put_user(srv, "guest", onAir="station").status_code == 200
        st = of_type(watcher, "stationChanged")
        assert st is not None and st["callsign"] == CLUB and st["onAirOwn"] is False, st
        acc = of_type(guest, "access")
        assert acc is not None and acc["user"]["onAir"] == "station", "the holder's page follows the account"

        assert put_user(srv, "guest", onAir="own").status_code == 200
        assert of_type(watcher, "stationChanged", lambda m: m["callsign"] == GUEST_CALL) is not None

        assert put_user(srv, "guest", callsign="f/k1fm/p").status_code == 200
        assert of_type(watcher, "stationChanged", lambda m: m["callsign"] == "F/K1FM/P") is not None
        assert put_user(srv, "guest", callsign=GUEST_CALL).status_code == 200
        assert of_type(watcher, "stationChanged", lambda m: m["callsign"] == GUEST_CALL) is not None

        # The station call changes under a guest on the seat: the on-air call stays theirs.
        r = srv.req("PUT", "/api/v1/station", json={"callsign": "W1NEW"}, cookie=srv.admin_cookie)
        assert r.status_code == 200 and r.json()["callsign"] == "W1NEW" and r.json()["onAir"] == GUEST_CALL, r.text
        st = of_type(watcher, "stationChanged")
        assert st is not None and st["callsign"] == GUEST_CALL and st["clubCallsign"] == "W1NEW", st
        srv.req("PUT", "/api/v1/station", json={"callsign": CLUB}, cookie=srv.admin_cookie)
        assert of_type(watcher, "stationChanged", lambda m: m.get("clubCallsign") == CLUB) is not None

        # The seat off: no single operator, everyone is on the station call.
        set_policy(srv, seat=False)
        st = of_type(watcher, "stationChanged")
        assert st is not None and st["callsign"] == CLUB and st["onAirOwn"] is False, st
        drain(guest)
        send(guest, "qsoLogged", qso=qso("DX6FF", "110000"))
        added = of_type(guest, "qsoAdded")
        assert added is not None and added["qso"]["stationCall"] == CLUB and added["qso"]["operator"] == GUEST_CALL, added
        w = rest(srv, "GET", "/api/v1/logbook/worked", "guest").json()
        assert w["callsign"] == CLUB, "with the seat off the guest transmits as the station"
    finally:
        set_policy(srv, seat=True)
        guest.close()
        watcher.close()


# ---------------------------------------------------------------------------
# the remote logger and the audit log
# ---------------------------------------------------------------------------


def test_remote_logger_gets_only_station_call_qsos(srv):
    for call in (b"DX1AA", b"DX3CC", b"DX4DD", b"DX6FF"):
        assert srv.sink.saw(call), f"{call!r} was made on the station call and is forwarded"
    time.sleep(0.5)
    for call in (b"DX2BB", b"DX5EE"):
        assert not any(call in d for d in srv.sink.datagrams), f"{call!r} is the guest's, not the station's"


def test_audit_lines(srv):
    audit = (srv.shared_dir / "audit.log").read_text()
    lines = audit.splitlines()

    def has(event, *needles):
        return any(f" {event} " in ln and all(n in ln for n in needles) for ln in lines)

    assert has("onair", "guest@", GUEST_CALL, "own call"), "the guest going on the air under their call"
    assert has("onair", CLUB, "station call"), "and the station call coming back"
    assert has("onair", "F/K1FM/P"), "a callsign edit of the holder"
    assert has("qso-edit", "mem@", "DX4DD") and has("qso-edit", "clubadmin@", "DX2BB")
    assert has("qso-delete", "guest@", "DX5EE") and has("qso-delete", "clubadmin@", "OLD1A")
    assert has("log-import", "clubadmin@")
    assert has("user-updated", "guest", "onair=own")
