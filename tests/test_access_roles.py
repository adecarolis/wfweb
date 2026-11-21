"""Shared Station, phase 2: what each access level may do, and the policy.

Its own --shared wfweb against its own mock radio (the lockout test of
test_access.py locks 127.0.0.1 for a minute, so the two modules never share
a server).  The admin is created through the setup code, three accounts
are added (listen / control / operate), and then:

- the WebSocket gate: every class of command against every role, a
  'denied' message naming the role it needs, nothing for what is allowed;
- TX audio (0x03 frames) from a listener: one 'denied', not one per frame;
- the REST gate with API tokens: GETs for everyone, writes by capability;
- the visitor policy (none / listen / control) for WebSockets and REST,
  applied to visitors already connected;
- TX inhibit: refused keys, a keyed operator released at once, rigctld
  and REST included;
- the TX time-out releasing a key held too long;
- a REST key in the audit log, under the time-out and the inhibit; the
  tuner over REST at operate level; a role change reaching open pages;
- TX inhibit sparing the log and STOP; CW named in the audit log;
- the connection cap and the admin's connection list with KICK;
- the audit log naming key-down / key-up / policy / kick.
"""

from __future__ import annotations

import json
import time

import pytest
import requests

from conftest import SharedServer, launch_shared_wfweb, set_cookie, _run_on_loop, _stop_wfweb
from mock_radio import MockIcomRadio

PASSWORDS = {"clubadmin": "club-admin-pw-2026", "lis": "listener-pw-1", "con": "control-pw-1", "op": "operator-pw-1"}


@pytest.fixture(scope="module")
def roles_mock(event_loop):
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


@pytest.fixture(scope="module")
def srv(roles_mock, event_loop, tmp_path_factory):
    home = tmp_path_factory.mktemp("roles-home")
    s = launch_shared_wfweb(roles_mock, event_loop, home)
    # Admin through the setup code, then one account per level.
    r = s.post("/api/v1/auth/setup", {"code": s.setup_code(), "username": "clubadmin",
                                       "password": PASSWORDS["clubadmin"], "callsign": "K1FM"})
    assert r.status_code == 200, r.text
    s.admin_cookie, _ = set_cookie(r)
    for name, role in (("lis", "listen"), ("con", "control"), ("op", "operate")):
        r = s.post("/api/v1/shared/users", {"username": name, "password": PASSWORDS[name], "role": role},
                   cookie=s.admin_cookie)
        assert r.status_code == 201, r.text
    # The seat (test_access_seat.py) is off here: this module is about the
    # roles alone.
    r = s.req("PUT", "/api/v1/shared/policy", json={"seat": False}, cookie=s.admin_cookie)
    assert r.status_code == 200, r.text
    s.cookies = {"clubadmin": s.admin_cookie}
    s.tokens = {}
    for name in ("lis", "con", "op"):
        r = s.post("/api/v1/auth/login", {"username": name, "password": PASSWORDS[name]})
        assert r.status_code == 200, r.text
        s.cookies[name], _ = set_cookie(r)
    for name in ("lis", "con", "op", "clubadmin"):
        r = s.post("/api/v1/auth/tokens", {"name": "t"}, cookie=s.cookies[name])
        assert r.status_code == 201, r.text
        s.tokens[name] = r.json()["token"]
    # The rig must be there before anyone keys it.
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


def recv_until(ws, pred, timeout=5.0):
    """Messages until pred(msg) is true; None when the time is up."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            msg = json.loads(ws.recv(timeout=max(0.05, deadline - time.monotonic())))
        except TimeoutError:
            return None
        if pred(msg):
            return msg
    return None


def denied_for(ws, cmd, timeout=0.8):
    """Send cmd; the 'denied' message for it within timeout, else None."""
    ws.send(json.dumps(cmd))
    return recv_until(ws, lambda m: m.get("type") == "denied" and m.get("cmd") == cmd["cmd"], timeout)


def open_ws(srv, name):
    """A WebSocket logged in as name (None: no cookie), past the access and
    rigInfo messages, with the access message returned alongside."""
    ws = srv.ws(cookie=srv.cookies[name] if name else None)
    access = recv_until(ws, lambda m: m.get("type") in ("access", "authRequired", "stationFull"))
    return ws, access


def set_policy(srv, **fields):
    r = srv.req("PUT", "/api/v1/shared/policy", json=fields, cookie=srv.admin_cookie)
    assert r.status_code == 200, r.text
    return r.json()["policy"]


def ptt_state(srv):
    return srv.get("/api/v1/radio/ptt", cookie=srv.admin_cookie).json().get("transmitting", False)


# ---------------------------------------------------------------------------
# The WebSocket gate
# ---------------------------------------------------------------------------


def test_access_message_describes_the_role(srv):
    ws, access = open_ws(srv, "lis")
    with ws:
        assert access["type"] == "access" and access["role"] == "listen" and access["anonymous"] is False
        assert access["can"] == {"control": False, "tx": False, "admin": False}
        assert access["denied"]["setFrequency"] == "control"
        assert access["denied"]["setPTT"] == "operate"
        assert access["denied"]["setStationCallsign"] == "admin"
        assert "getStatus" not in access["denied"]
        assert access["policy"]["anonymous"] == "none"
    ws, access = open_ws(srv, "clubadmin")
    with ws:
        assert access["role"] == "admin" and access["denied"] == {} and access["can"]["admin"] is True


@pytest.mark.parametrize("name,cmd,need", [
    ("lis", {"cmd": "getStatus"}, None),
    ("lis", {"cmd": "enableAudio", "value": True}, None),
    ("lis", {"cmd": "setFrequency", "value": 7074000}, "control"),
    ("lis", {"cmd": "setPTT", "value": True}, "operate"),
    ("lis", {"cmd": "setStationCallsign", "callsign": "X1X"}, "admin"),
    ("con", {"cmd": "setFrequency", "value": 7074000}, None),
    ("con", {"cmd": "setMode", "value": "USB"}, None),
    ("con", {"cmd": "setPTT", "value": True}, "operate"),
    ("con", {"cmd": "sendCW", "text": "TEST"}, "operate"),
    ("con", {"cmd": "qsoLogged", "qso": {}}, "operate"),
    ("con", {"cmd": "setCWMacros", "macros": {}}, "admin"),
    ("op", {"cmd": "setFrequency", "value": 7074000}, None),
    ("op", {"cmd": "setPower", "value": True}, "admin"),
    ("op", {"cmd": "writeMemory", "channel": 1}, "admin"),
    ("op", {"cmd": "noSuchCommandEver"}, "admin"),
    ("clubadmin", {"cmd": "setFrequency", "value": 7074000}, None),
])
def test_command_gate(srv, name, cmd, need):
    ws, _ = open_ws(srv, name)
    with ws:
        d = denied_for(ws, cmd)
        if need is None:
            assert d is None, f"{name} was refused {cmd['cmd']}: {d}"
        else:
            assert d is not None, f"{name} was allowed {cmd['cmd']}"
            assert d["need"] == need
            assert need in d["reason"]


def test_operator_keys_and_unkeys(srv):
    ws, _ = open_ws(srv, "op")
    with ws:
        assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not ptt_state(srv):
            time.sleep(0.1)
        assert ptt_state(srv) is True
        assert denied_for(ws, {"cmd": "setPTT", "value": False}) is None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and ptt_state(srv):
            time.sleep(0.1)
        assert ptt_state(srv) is False


def test_tx_audio_from_a_listener_is_dropped_once(srv):
    ws, _ = open_ws(srv, "lis")
    with ws:
        frame = bytes([0x03, 0x00, 0, 0, 0, 0]) + bytes(640)
        ws.send(frame)
        d = recv_until(ws, lambda m: m.get("type") == "denied", 2.0)
        assert d is not None and d["cmd"] == "txAudio" and d["need"] == "operate"
        ws.send(frame)
        ws.send(frame)
        assert recv_until(ws, lambda m: m.get("type") == "denied", 1.0) is None, "one warning per stream"
    ws, _ = open_ws(srv, "op")
    with ws:
        ws.send(frame)
        assert recv_until(ws, lambda m: m.get("type") == "denied", 1.0) is None, "an operator's audio passes"


# ---------------------------------------------------------------------------
# The REST gate
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("name,method,path,body,status", [
    ("lis", "GET", "/api/v1/radio/info", None, 200),
    ("lis", "GET", "/api/v1/radio/frequency", None, 200),
    ("lis", "PUT", "/api/v1/radio/frequency", {"hz": 7074000}, 403),
    ("lis", "PUT", "/api/v1/radio/ptt", {"transmitting": True}, 403),
    ("con", "PUT", "/api/v1/radio/frequency", {"hz": 7074000}, 403),
    ("con", "PUT", "/api/v1/radio/ptt", {"transmitting": True}, 403),
    ("con", "POST", "/api/v1/radio/cw", {"text": "TEST"}, 403),
    ("op", "GET", "/api/v1/radio/ptt", None, 200),
    ("con", "PUT", "/api/v1/station", {"callsign": "X1X"}, 403),
    ("op", "PUT", "/api/v1/station", {"callsign": "X1X"}, 403),
    ("op", "PUT", "/api/v1/cw/macros", {"macros": {}}, 403),
    ("op", "GET", "/api/v1/shared/users", None, 403),
    ("clubadmin", "GET", "/api/v1/shared/users", None, 200),
    ("clubadmin", "PUT", "/api/v1/station", {"callsign": "K1FM", "grid": "FN42"}, 200),
])
def test_rest_gate(srv, name, method, path, body, status):
    r = srv.req(method, path, json=body, token=srv.tokens[name], plain=True) if body is not None \
        else srv.req(method, path, token=srv.tokens[name], plain=True)
    assert r.status_code == status, f"{name} {method} {path}: {r.status_code} {r.text}"
    if status == 403:
        assert "access" in r.json()["error"] or "admin" in r.json()["error"] or "read-only" in r.json()["error"]


# ---------------------------------------------------------------------------
# Visitors
# ---------------------------------------------------------------------------


def test_visitor_policy(srv):
    pytest.importorskip("websockets")
    from websockets.exceptions import ConnectionClosed

    ws, first = open_ws(srv, None)
    with ws:
        assert first["type"] == "authRequired", "visitors refused by default"
    assert srv.get("/api/v1/radio/info").status_code == 401

    assert set_policy(srv, anonymous="listen")["anonymous"] == "listen"
    assert srv.get("/api/v1/radio/info").status_code == 200, "a visitor may read"
    assert srv.req("PUT", "/api/v1/radio/frequency", json={"hz": 7074000}).status_code == 403
    ws, first = open_ws(srv, None)
    with ws:
        assert first["type"] == "access" and first["anonymous"] is True and first["role"] == "listen"
        assert first["user"] is None
        d = denied_for(ws, {"cmd": "setFrequency", "value": 7074000})
        assert d is not None and d["need"] == "control"
        assert denied_for(ws, {"cmd": "getStatus"}) is None

        # Raised to control while connected: the same socket learns it.
        set_policy(srv, anonymous="control")
        upd = recv_until(ws, lambda m: m.get("type") == "access", 5.0)
        assert upd is not None and upd["role"] == "control"
        assert denied_for(ws, {"cmd": "setFrequency", "value": 7074000}) is None
        assert srv.req("PUT", "/api/v1/radio/frequency", json={"hz": 7074000}).status_code == 403, \
            "the page tunes; REST never does"

        # Visitors refused again: the socket is closed with 4401.
        set_policy(srv, anonymous="none")
        closed = None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                ws.recv(timeout=2)
            except ConnectionClosed as e:
                closed = e.rcvd.code
                break
            except TimeoutError:
                break
        assert closed == 4401
    assert srv.get("/api/v1/radio/info").status_code == 401

    r = srv.req("PUT", "/api/v1/shared/policy", json={"anonymous": "admin"}, cookie=srv.admin_cookie)
    assert r.status_code == 400, "a visitor is never more than control"
    assert srv.req("PUT", "/api/v1/shared/policy", json={"anonymous": "listen"}, cookie=srv.cookies["op"]).status_code == 403


# ---------------------------------------------------------------------------
# TX inhibit and time-out
# ---------------------------------------------------------------------------


def test_tx_inhibit(srv):
    ws, _ = open_ws(srv, "op")
    with ws:
        # Keyed, then inhibited: released at once and told why.
        assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not ptt_state(srv):
            time.sleep(0.1)
        assert ptt_state(srv) is True
        set_policy(srv, txInhibit=True)
        d = recv_until(ws, lambda m: m.get("type") == "denied" and m.get("cmd") == "setPTT", 5.0)
        assert d is not None and "inhibit" in d["reason"]
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and ptt_state(srv):
            time.sleep(0.1)
        assert ptt_state(srv) is False
        pol = recv_until(ws, lambda m: m.get("type") == "sharedPolicy", 2.0)
        assert pol is not None and pol["policy"]["txInhibit"] is True

        d = denied_for(ws, {"cmd": "setPTT", "value": True})
        assert d is not None and "inhibit" in d["reason"]
        d = denied_for(ws, {"cmd": "sendCW", "text": "TEST"})
        assert d is not None and "inhibit" in d["reason"]
        assert denied_for(ws, {"cmd": "setFrequency", "value": 7074000}) is None, "tuning is not transmitting"
        r = srv.req("PUT", "/api/v1/radio/ptt", json={"transmitting": True}, token=srv.tokens["op"], plain=True)
        assert r.status_code == 403
        assert ptt_state(srv) is False

        set_policy(srv, txInhibit=False)
        assert recv_until(ws, lambda m: m.get("type") == "sharedPolicy", 2.0)["policy"]["txInhibit"] is False
        assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
        assert denied_for(ws, {"cmd": "setPTT", "value": False}) is None


def _tune(srv, ws, hz):
    assert denied_for(ws, {"cmd": "setFrequency", "value": hz}) is None
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if srv.get("/api/v1/radio/frequency", cookie=srv.admin_cookie).json().get("hz") == hz:
            return
        time.sleep(0.1)
    raise AssertionError(f"the rig did not go to {hz}")


def test_tx_inhibit_per_band(srv):
    """A band closed to transmitting: for everyone, admins included, on that
    band only; tuning and the rest of the station are untouched."""
    ws, _ = open_ws(srv, "op")
    adm, _ = open_ws(srv, "clubadmin")
    with ws, adm:
        _tune(srv, ws, 14074000)
        pol = set_policy(srv, txInhibitBands=["20m", "20M", "Gen", "", "60m"])
        assert pol["txInhibitBands"] == ["20m", "60m"], "duplicates, blanks and Gen dropped"
        assert pol["txInhibit"] is False
        for sock in (ws, adm):
            d = denied_for(sock, {"cmd": "setPTT", "value": True})
            assert d is not None and "on 20m" in d["reason"]
        d = denied_for(ws, {"cmd": "sendCW", "text": "TEST"})
        assert d is not None and "on 20m" in d["reason"]
        r = srv.req("PUT", "/api/v1/radio/ptt", json={"transmitting": True}, token=srv.tokens["op"], plain=True)
        assert r.status_code == 403
        assert ptt_state(srv) is False

        # Another band: the key works; tuned onto the closed one, it is released.
        _tune(srv, ws, 7074000)
        assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not ptt_state(srv):
            time.sleep(0.1)
        assert ptt_state(srv) is True
        # (The 'denied' that says why may arrive while _tune reads the
        # socket: the audit log is checked for it below.)
        _tune(srv, ws, 14074000)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and ptt_state(srv):
            time.sleep(0.1)
        assert ptt_state(srv) is False

        # Closing the band under a held key releases it too.
        _tune(srv, ws, 7074000)
        assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not ptt_state(srv):
            time.sleep(0.1)
        set_policy(srv, txInhibitBands=["40m"])
        d = recv_until(ws, lambda m: m.get("type") == "denied" and m.get("cmd") == "setPTT", 5.0)
        assert d is not None and "on 40m" in d["reason"]
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and ptt_state(srv):
            time.sleep(0.1)
        assert ptt_state(srv) is False

        assert set_policy(srv, txInhibitBands=[])["txInhibitBands"] == []
        assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
        assert denied_for(ws, {"cmd": "setPTT", "value": False}) is None
    audit = (srv.shared_dir / "audit.log").read_text()
    assert "tx-inhibit-bands=20m,60m" in audit and "tx-inhibit-bands=none" in audit
    assert "unkey op@127.0.0.1 transmitting on 20m is inhibited" in audit
    assert "unkey op@127.0.0.1 transmitting on 40m is inhibited" in audit


def test_tx_timeout(srv):
    # The socket first: without the websockets package this test is skipped
    # there, and must not leave the 2 s time-out behind for the next one.
    ws, _ = open_ws(srv, "op")
    set_policy(srv, txTimeoutSecs=2)
    with ws:
        assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
        d = recv_until(ws, lambda m: m.get("type") == "denied" and m.get("cmd") == "setPTT", 6.0)
        assert d is not None and "time-out" in d["reason"]
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and ptt_state(srv):
            time.sleep(0.1)
        assert ptt_state(srv) is False
    set_policy(srv, txTimeoutSecs=600)
    audit = (srv.shared_dir / "audit.log").read_text()
    assert "key-down op@127.0.0.1" in audit
    assert "key-up op@127.0.0.1" in audit
    assert "unkey op@127.0.0.1 TX time-out" in audit
    assert "policy clubadmin@127.0.0.1" in audit and "tx-timeout=2" in audit


# ---------------------------------------------------------------------------
# Connections: the cap, the list, KICK
# ---------------------------------------------------------------------------


def _wait_ptt(srv, want, secs=5.0):
    deadline = time.monotonic() + secs
    while time.monotonic() < deadline and ptt_state(srv) is not want:
        time.sleep(0.1)
    return ptt_state(srv) is want


def _audit_mark(srv):
    return len((srv.shared_dir / "audit.log").read_text().splitlines())


def _audit_since(srv, mark):
    return (srv.shared_dir / "audit.log").read_text().splitlines()[mark:]


def test_rest_does_not_touch_the_radio(srv):
    """A shared station is operated from the page, by whoever has the seat:
    over REST the radio is read, never set -- not tuned, not keyed, not
    stopped -- by any account, the admin's included, with a token or a
    cookie.  A script reads; what is not the radio (the log, the station
    configuration) is still REST's."""
    mark = _audit_mark(srv)
    before = srv.get("/api/v1/radio/frequency", cookie=srv.admin_cookie).json().get("hz")
    writes = (("PUT", "/api/v1/radio/ptt", {"transmitting": True}),
              ("PUT", "/api/v1/radio/ptt", {"transmitting": False}),
              ("POST", "/api/v1/radio/cw", {"text": "TEST"}),
              ("DELETE", "/api/v1/radio/cw", None),
              ("PUT", "/api/v1/radio/tx", {"tuner": 2}),
              ("PUT", "/api/v1/radio/tx", {"tuner": 1}),
              ("PUT", "/api/v1/radio/tx", {"split": False}),
              ("PUT", "/api/v1/radio/frequency", {"hz": 7123000}),
              ("PUT", "/api/v1/radio/mode", {"mode": "CW"}),
              ("POST", "/api/v1/radio/memories/1/recall", None))
    for name in ("lis", "con", "op", "clubadmin"):
        for method, path, body in writes:
            r = srv.req(method, path, json=body, token=srv.tokens[name], plain=True)
            assert r.status_code == 403 and "read-only" in r.json()["error"], (name, method, path, r.text)
    for method, path, body in writes:
        r = srv.req(method, path, json=body, cookie=srv.admin_cookie)
        assert r.status_code == 403 and "read-only" in r.json()["error"], (method, path, r.text)
    set_policy(srv, anonymous="control")
    try:
        r = srv.req("PUT", "/api/v1/radio/frequency", json={"hz": 7123000}, plain=True)
        assert r.status_code == 403 and "read-only" in r.json()["error"], "a visitor admitted to tune tunes on the page"
    finally:
        set_policy(srv, anonymous="none")
    time.sleep(0.5)
    assert ptt_state(srv) is False
    assert srv.get("/api/v1/radio/frequency", cookie=srv.admin_cookie).json().get("hz") == before
    lines = _audit_since(srv, mark)
    assert not any(" key-down " in l or " tx " in l for l in lines), lines

    for path in ("/api/v1/radio/info", "/api/v1/radio/frequency", "/api/v1/radio/mode", "/api/v1/radio/ptt",
                 "/api/v1/radio/status"):
        assert srv.get(path, token=srv.tokens["lis"], plain=True).status_code == 200, path
    r = srv.req("POST", "/api/v1/logbook", json={}, token=srv.tokens["op"], plain=True)
    assert r.status_code != 403, "the log is not the radio"


def test_role_change_reaches_open_pages(srv):
    """An account's level changed by the admin applies to its open pages at
    once.  The socket used to keep the level it connected with, so a demoted
    operator went on transmitting until they reloaded."""
    users = srv.get("/api/v1/shared/users", cookie=srv.admin_cookie).json()["users"]
    op_id = next(u["id"] for u in users if u["name"] == "op")

    def set_role(role):
        return srv.req("PUT", f"/api/v1/shared/users/{op_id}", json={"role": role}, cookie=srv.admin_cookie)

    ws, access = open_ws(srv, "op")
    with ws:
        try:
            assert access["role"] == "operate"
            assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
            assert _wait_ptt(srv, True)
            assert set_role("control").status_code == 200
            a = recv_until(ws, lambda m: m.get("type") == "access", 3.0)
            assert a is not None and a["role"] == "control" and a["can"]["tx"] is False
            told = recv_until(ws, lambda m: m.get("type") == "denied" and "access level" in m.get("reason", ""), 3.0)
            assert told is not None, "the page is told why its key was released"
            assert _wait_ptt(srv, False), "a key held by a demoted account is released"
            d = denied_for(ws, {"cmd": "setPTT", "value": True})
            assert d is not None and d["need"] == "operate"
            assert denied_for(ws, {"cmd": "setFrequency", "value": 7074000}) is None, "control still tunes"
        finally:
            r = set_role("operate")
        assert r.status_code == 200
        a = recv_until(ws, lambda m: m.get("type") == "access", 3.0)
        assert a is not None and a["role"] == "operate" and a["can"]["tx"] is True
        assert denied_for(ws, {"cmd": "setPTT", "value": True}) is None
        assert denied_for(ws, {"cmd": "setPTT", "value": False}) is None
    assert ptt_state(srv) is False


def test_tx_inhibit_spares_the_log_and_stop(srv):
    """TX inhibit stops what transmits.  It used to refuse every operate-level
    command, so nobody could log a QSO or stop the keyer while it was on."""
    def rest(method, path, body=None):
        return srv.req(method, path, json=body, token=srv.tokens["op"], plain=True)

    ws, _ = open_ws(srv, "op")
    with ws:
        set_policy(srv, txInhibit=True)
        try:
            assert denied_for(ws, {"cmd": "qsoLogged", "qso": {}}) is None, "the log is not a transmission"
            assert denied_for(ws, {"cmd": "stopCW"}) is None, "STOP is always allowed"
            for cmd in ({"cmd": "sendCW", "text": "TEST"}, {"cmd": "setTuner", "value": 1}, {"cmd": "enableMic", "value": True}):
                d = denied_for(ws, cmd)
                assert d is not None and "inhibit" in d["reason"], cmd
            assert rest("POST", "/api/v1/logbook", {}).status_code != 403, "logging over REST too"
        finally:
            set_policy(srv, txInhibit=False)
    assert ptt_state(srv) is False


def test_on_air_commands_are_audited(srv):
    """CW through the rig's keyer has no key-down of its own: a 'tx' line
    names the account, one a minute at most however many characters go out."""
    mark = _audit_mark(srv)
    ws, _ = open_ws(srv, "op")
    with ws:
        for _ in range(3):
            assert denied_for(ws, {"cmd": "sendCW", "text": "E"}, timeout=0.3) is None
    time.sleep(0.3)
    audit = (srv.shared_dir / "audit.log").read_text().splitlines()
    assert any(" tx op@127.0.0.1 cw" in l for l in audit), "a browser's CW is named"
    new = _audit_since(srv, mark)
    assert sum(" tx op@127.0.0.1 cw" in l for l in new) <= 1, new


def test_connection_cap_and_kick(srv):
    pytest.importorskip("websockets")
    from websockets.exceptions import ConnectionClosed

    set_policy(srv, maxClients=1)
    first, a1 = open_ws(srv, "lis")
    with first:
        assert a1["type"] == "access"
        second, a2 = open_ws(srv, "con")
        with second:
            assert a2["type"] == "stationFull" and a2["max"] == 1
        admin, a3 = open_ws(srv, "clubadmin")
        with admin:
            assert a3["type"] == "access", "admins always get in"

            conns = srv.get("/api/v1/shared/connections", cookie=srv.admin_cookie).json()
            names = sorted(c["name"] for c in conns["connections"])
            assert names == ["clubadmin", "lis"], names
            assert conns["max"] == 1
            victim = [c for c in conns["connections"] if c["name"] == "lis"][0]
            assert victim["role"] == "listen" and victim["ip"] == "127.0.0.1" and victim["keying"] is False

            r = srv.req("DELETE", f"/api/v1/shared/connections/{victim['id']}", cookie=srv.admin_cookie)
            assert r.status_code == 200
            closed = None
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                try:
                    first.recv(timeout=2)
                except ConnectionClosed as e:
                    closed = e.rcvd.code
                    break
                except TimeoutError:
                    break
            assert closed == 4401
            mine = [c for c in conns["connections"] if c["name"] == "clubadmin"][0]
            r = srv.req("DELETE", f"/api/v1/shared/connections/{mine['id']}", cookie=srv.admin_cookie)
            assert r.status_code == 409, "not your own connection"
    set_policy(srv, maxClients=20)
    audit = (srv.shared_dir / "audit.log").read_text()
    assert "kick clubadmin@127.0.0.1 lis@127.0.0.1" in audit


def test_policy_is_read_back(srv):
    pol = srv.get("/api/v1/shared/policy", cookie=srv.admin_cookie).json()["policy"]
    assert pol == {"anonymous": "none", "txInhibit": False, "txInhibitBands": [], "txTimeoutSecs": 600, "maxClients": 20,
                   "seat": False, "seatIdleSecs": 600, "seatRequestSecs": 60, "seatMaxSecs": 0,
                   "seatPreemptHigher": False, "listenIn": True, "allowPrivate": False}
    assert srv.get("/api/v1/shared/policy", cookie=srv.cookies["op"]).status_code == 403
