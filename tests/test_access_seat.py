"""Shared Station, phase 3: the seat -- one browser drives the radio.

Its own --shared wfweb and mock radio (as test_access_roles.py).  Accounts:
the admin, con1 / con2 (control), op1 / op2 (operate), lis (listen).  Then:

- the seat message on connect, and the gate: tuning needs the seat, the
  holder tunes, everyone else is told who has it;
- taking it when free; asking for it when held, the holder's prompt,
  DECLINE and HAND OVER;
- the queue: an offer to the first in line when the seat frees, taken or
  passed;
- the grace period: a holder whose socket drops gets the seat back on
  reconnecting, and keeps everyone else off it meanwhile;
- the clocks: a request granted when unanswered, the idle release, time's
  up when others wait;
- the admin taking the seat outright, and clearing it over REST;
- TX audio and PTT follow the seat (a keyed holder is released on hand-over);
- REST writes: free seat or the same account, else 409;
- private operation and listen-in, as reported in the seat message;
- the seat switched off.
"""

from __future__ import annotations

import json
import time

import pytest

from conftest import launch_shared_wfweb, set_cookie, _run_on_loop, _stop_wfweb
from mock_radio import MockIcomRadio

PASSWORDS = {"clubadmin": "club-admin-pw-2026", "con1": "control-pw-one", "con2": "control-pw-two",
             "op1": "operator-pw-one", "op2": "operator-pw-two", "lis": "listener-pw-1"}
ROLES = {"con1": "control", "con2": "control", "op1": "operate", "op2": "operate", "lis": "listen"}


@pytest.fixture(scope="module")
def seat_mock(event_loop):
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


@pytest.fixture(scope="module")
def srv(seat_mock, event_loop, tmp_path_factory):
    s = launch_shared_wfweb(seat_mock, event_loop, tmp_path_factory.mktemp("seat-home"))
    r = s.post("/api/v1/auth/setup", {"code": s.setup_code(), "username": "clubadmin",
                                       "password": PASSWORDS["clubadmin"], "callsign": "K1FM"})
    assert r.status_code == 200, r.text
    s.admin_cookie, _ = set_cookie(r)
    s.cookies = {"clubadmin": s.admin_cookie}
    s.tokens = {}
    for name, role in ROLES.items():
        r = s.post("/api/v1/shared/users", {"username": name, "password": PASSWORDS[name], "role": role}, cookie=s.admin_cookie)
        assert r.status_code == 201, r.text
        r = s.post("/api/v1/auth/login", {"username": name, "password": PASSWORDS[name]})
        assert r.status_code == 200, r.text
        s.cookies[name], _ = set_cookie(r)
    for name in ("con1", "op1", "clubadmin"):
        r = s.post("/api/v1/auth/tokens", {"name": "t"}, cookie=s.cookies[name])
        assert r.status_code == 201, r.text
        s.tokens[name] = r.json()["token"]
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
        if pred(msg):
            return msg
    return None


def seat_msg(ws, pred=lambda m: True, timeout=5.0):
    """The next 'seat' message matching pred."""
    return recv_until(ws, lambda m: m.get("type") == "seat" and pred(m), timeout)


def drain(ws, seconds=0.3):
    """Whatever is queued on the socket, so a later wait sees fresh messages."""
    out = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            out.append(json.loads(ws.recv(timeout=max(0.05, deadline - time.monotonic()))))
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
    """A socket logged in as name, past the access message; returns (ws, seat)."""
    ws = srv.ws(cookie=srv.cookies[name])
    access = recv_until(ws, lambda m: m.get("type") == "access")
    assert access is not None and access["shared"] is True
    seat = seat_msg(ws)
    assert seat is not None, "a seat message follows the access message"
    return ws, seat


def set_policy(srv, **fields):
    r = srv.req("PUT", "/api/v1/shared/policy", json=fields, cookie=srv.admin_cookie)
    assert r.status_code == 200, r.text
    return r.json()["policy"]


def rest_seat(srv):
    return srv.get("/api/v1/shared/seat", cookie=srv.admin_cookie).json()["seat"]


def ptt_state(srv):
    return srv.get("/api/v1/radio/ptt", cookie=srv.admin_cookie).json().get("transmitting", False)


def wait_ptt(srv, value, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and ptt_state(srv) != value:
        time.sleep(0.1)
    assert ptt_state(srv) == value


@pytest.fixture(autouse=True)
def seat_cleared(srv):
    """Every test starts and ends with a free seat and nobody in line."""
    srv.req("DELETE", "/api/v1/shared/seat", cookie=srv.admin_cookie)
    yield
    srv.req("DELETE", "/api/v1/shared/seat", cookie=srv.admin_cookie)


# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------


def test_seat_message_and_gate(srv):
    con1, seat = open_ws(srv, "con1")
    with con1:
        assert seat["enabled"] is True and seat["holder"] is None and seat["mine"] is False
        assert seat["queue"] == [] and seat["position"] == 0
        d = denied_for(con1, "setFrequency", value=7074000)
        assert d is not None and d["need"] == "seat" and "take the seat" in d["reason"]
        assert denied_for(con1, "getStatus") is None, "watching needs no seat"
        assert denied_for(con1, "setTxMeter", value="swr") is None, "which meter to watch is the browser's own"

        send(con1, "seatTake")
        seat = seat_msg(con1, lambda m: m["mine"] is True)
        assert seat is not None and seat["holder"]["name"] == "con1" and seat["holder"]["role"] == "control"
        assert denied_for(con1, "setFrequency", value=7074000) is None, "the holder tunes"
        d = denied_for(con1, "setPTT", value=True)
        assert d is not None and d["need"] == "operate", "the seat does not add to the role"

        con2, seat2 = open_ws(srv, "con2")
        with con2:
            assert seat2["holder"]["name"] == "con1" and seat2["mine"] is False
            d = denied_for(con2, "setFrequency", value=7074000)
            assert d is not None and d["need"] == "seat" and "con1 has the seat" in d["reason"]
        assert rest_seat(srv)["holder"]["name"] == "con1"


def test_request_decline_handover(srv):
    con1, _ = open_ws(srv, "con1")
    con2, _ = open_ws(srv, "con2")
    with con1, con2:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        drain(con2)

        send(con2, "seatTake")   # held: a request
        req = seat_msg(con1, lambda m: m.get("request") is not None)
        assert req is not None and req["request"]["name"] == "con2" and req["request"]["secs"] > 0
        mine = seat_msg(con2, lambda m: m.get("requested") is not None)
        assert mine is not None and mine["position"] == 1 and mine["queue"][0]["name"] == "con2"

        send(con1, "seatHandover", accept=False)
        note = recv_until(con2, lambda m: m.get("type") == "seatNotice")
        assert note is not None and "declined" in note["text"]
        after = seat_msg(con1, lambda m: m.get("request") is None)
        assert after is not None and after["mine"] is True, "the holder keeps the seat"
        still = seat_msg(con2, lambda m: m.get("requested") is None)
        assert still is not None and still["position"] == 1, "the asker stays in line"

        drain(con1); drain(con2)
        send(con2, "seatTake")
        assert seat_msg(con1, lambda m: m.get("request") is not None) is not None
        send(con1, "seatHandover", accept=True)
        lost = recv_until(con1, lambda m: m.get("type") == "seatLost")
        assert lost is not None and lost["to"] == "con2"
        got = seat_msg(con2, lambda m: m["mine"])
        assert got is not None and got["holder"]["name"] == "con2" and got["queue"] == []
        assert denied_for(con2, "setFrequency", value=7074000) is None
        assert denied_for(con1, "setFrequency", value=7074000)["reason"] == "con2 has the seat"


def test_queue_offer_taken_and_passed(srv):
    con1, _ = open_ws(srv, "con1")
    op1, _ = open_ws(srv, "op1")
    op2, _ = open_ws(srv, "op2")
    with con1, op1, op2:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        drain(op1); drain(op2)
        send(op1, "seatTake")
        assert seat_msg(op1, lambda m: m.get("requested") is not None) is not None
        send(op2, "seatTake")
        note = recv_until(op2, lambda m: m.get("type") == "seatNotice")
        assert note is not None and "asked first" in note["text"]
        pos = seat_msg(op2, lambda m: m["position"] == 2)
        assert pos is not None and [q["name"] for q in pos["queue"]] == ["op1", "op2"]

        # The holder releases: the first in line is offered the seat, and passes.
        drain(con1); drain(op1); drain(op2)
        send(con1, "seatRelease")
        offer = seat_msg(op1, lambda m: m.get("offer") is not None)
        assert offer is not None and offer["holder"] is None and 0 < offer["offer"]["secs"] <= 30
        send(op1, "seatCancel")
        offer2 = seat_msg(op2, lambda m: m.get("offer") is not None)
        assert offer2 is not None, "the next in line is offered it"
        send(op2, "seatAccept")
        got = seat_msg(op2, lambda m: m["mine"])
        assert got is not None and got["holder"]["name"] == "op2" and got["queue"] == []
        assert seat_msg(op1, lambda m: m["holder"] is not None and m["position"] == 0) is not None


def test_grace_period(srv):
    op1, _ = open_ws(srv, "op1")
    con1, _ = open_ws(srv, "con1")
    send(op1, "seatTake")
    assert seat_msg(op1, lambda m: m["mine"]) is not None
    drain(con1)
    op1.close()   # the phone lost the network
    with con1:
        grace = seat_msg(con1, lambda m: m.get("grace") is not None)
        assert grace is not None and grace["holder"] is None and grace["grace"]["name"] == "op1"
        d = denied_for(con1, "seatTake")
        assert d is not None and "kept for op1" in d["reason"]
        d = denied_for(con1, "setFrequency", value=7074000)
        assert d is not None and d["need"] == "seat"
        # Back within the grace period: the seat is theirs again.
        back, seat = open_ws(srv, "op1")
        with back:
            assert seat["mine"] is True and seat["holder"]["name"] == "op1"
            assert seat_msg(con1, lambda m: m["holder"] is not None and m["holder"]["name"] == "op1") is not None
            assert denied_for(back, "setFrequency", value=7074000) is None


def test_request_granted_when_unanswered(srv):
    set_policy(srv, seatRequestSecs=2)
    con1, _ = open_ws(srv, "con1")
    con2, _ = open_ws(srv, "con2")
    with con1, con2:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        drain(con2)
        send(con2, "seatTake")
        assert seat_msg(con2, lambda m: m.get("requested") is not None) is not None
        got = seat_msg(con2, lambda m: m["mine"], timeout=6.0)
        assert got is not None, "an unanswered request is granted"
        lost = recv_until(con1, lambda m: m.get("type") == "seatLost")
        assert lost is not None and "unanswered" in lost["reason"]
    set_policy(srv, seatRequestSecs=60)


def test_idle_release(srv):
    set_policy(srv, seatIdleSecs=2)
    con1, _ = open_ws(srv, "con1")
    with con1:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        lost = recv_until(con1, lambda m: m.get("type") == "seatLost", 6.0)
        assert lost is not None and "idle" in lost["reason"]
        assert seat_msg(con1, lambda m: m["holder"] is None) is not None
    set_policy(srv, seatIdleSecs=600)


def test_max_time_when_others_wait(srv):
    set_policy(srv, seatMaxSecs=2)
    con1, _ = open_ws(srv, "con1")
    con2, _ = open_ws(srv, "con2")
    with con1, con2:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        # Alone, time never runs out: keep the seat busy for 3 s.
        for _ in range(6):
            send(con1, "setFrequency", value=7074000)
            time.sleep(0.5)
        assert recv_until(con1, lambda m: m.get("type") == "seatLost", 0.5) is None
        drain(con2)
        send(con2, "seatTake")   # somebody waiting: the clock runs from the take
        lost = recv_until(con1, lambda m: m.get("type") == "seatLost", 6.0)
        assert lost is not None and "others are waiting" in lost["reason"]
        offer = seat_msg(con2, lambda m: m.get("offer") is not None)
        assert offer is not None
    set_policy(srv, seatMaxSecs=0)


def test_admin_takes_and_clears(srv):
    con1, _ = open_ws(srv, "con1")
    admin, _ = open_ws(srv, "clubadmin")
    with con1, admin:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        drain(admin)
        send(admin, "seatTake")   # a plain take is a request, admin or not
        assert seat_msg(admin, lambda m: m.get("requested") is not None) is not None
        send(admin, "seatCancel")
        send(admin, "seatTake", force=True)
        got = seat_msg(admin, lambda m: m["mine"])
        assert got is not None
        lost = recv_until(con1, lambda m: m.get("type") == "seatLost")
        assert lost is not None and "admin" in lost["reason"]
        r = srv.req("DELETE", "/api/v1/shared/seat", cookie=srv.cookies["con1"])
        assert r.status_code == 403
        r = srv.req("DELETE", "/api/v1/shared/seat", cookie=srv.admin_cookie)
        assert r.status_code == 200
        assert seat_msg(admin, lambda m: m["holder"] is None) is not None
        assert rest_seat(srv)["holder"] is None
        conns = srv.get("/api/v1/shared/connections", cookie=srv.admin_cookie).json()["connections"]
        assert all(c["seat"] is False for c in conns)


def test_tx_follows_the_seat(srv):
    op1, _ = open_ws(srv, "op1")
    op2, _ = open_ws(srv, "op2")
    with op1, op2:
        frame = bytes([0x03, 0x00, 0, 0, 0, 0]) + bytes(640)
        d = denied_for(op1, "setPTT", value=True)
        assert d is not None and d["need"] == "seat"
        op1.send(frame)
        d = recv_until(op1, lambda m: m.get("type") == "denied" and m.get("cmd") == "txAudio", 2.0)
        assert d is not None and d["need"] == "seat"

        send(op1, "seatTake")
        assert seat_msg(op1, lambda m: m["mine"]) is not None
        assert denied_for(op1, "setPTT", value=True) is None
        wait_ptt(srv, True)
        op1.send(frame)
        assert recv_until(op1, lambda m: m.get("type") == "denied", 0.8) is None, "the holder's audio passes"
        seat = seat_msg(op1, lambda m: m["holder"]["keying"] is True, 3.0)
        assert seat is not None or rest_seat(srv)["holder"]["keying"] is True

        # The admin takes the seat while op1 is keyed: op1 is released first.
        admin, _ = open_ws(srv, "clubadmin")
        with admin:
            send(admin, "seatTake", force=True)
            assert seat_msg(admin, lambda m: m["mine"]) is not None
            wait_ptt(srv, False)
            d = recv_until(op1, lambda m: m.get("type") == "denied" and m.get("cmd") == "setPTT", 3.0)
            assert d is not None and "seat" in d["reason"]


def test_rest_writes_and_the_seat(srv):
    r = srv.req("PUT", "/api/v1/radio/frequency", json={"hz": 7074000}, token=srv.tokens["op1"], plain=True)
    assert r.status_code == 202, "a free seat: the script tunes"
    con1, _ = open_ws(srv, "con1")
    with con1:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        r = srv.req("PUT", "/api/v1/radio/frequency", json={"hz": 7074000}, token=srv.tokens["op1"], plain=True)
        assert r.status_code == 409 and "con1 has the seat" in r.json()["error"]
        r = srv.req("PUT", "/api/v1/radio/frequency", json={"hz": 7074000}, token=srv.tokens["con1"], plain=True)
        assert r.status_code == 202, "the holder's own script tunes"
        r = srv.get("/api/v1/radio/frequency", token=srv.tokens["op1"], plain=True)
        assert r.status_code == 200, "reading never needs the seat"
        r = srv.req("PUT", "/api/v1/station", json={"callsign": "K1FM"}, token=srv.tokens["clubadmin"], plain=True)
        assert r.status_code == 200, "station configuration is not the radio"


def test_private_and_listen_in(srv):
    con1, _ = open_ws(srv, "con1")
    lis, _ = open_ws(srv, "lis")
    with con1, lis:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        d = denied_for(con1, "seatPrivate", value=True)
        assert d is not None and "not allowed" in d["reason"]
        set_policy(srv, allowPrivate=True)
        drain(con1); drain(lis)
        send(con1, "seatPrivate", value=True)
        seat = seat_msg(lis, lambda m: m.get("privateNow") is True)
        assert seat is not None and seat["holder"]["private"] is True
        send(con1, "seatPrivate", value=False)
        assert seat_msg(lis, lambda m: m.get("privateNow") is False) is not None
        # No listen-in: private whether the holder wants it or not.
        set_policy(srv, listenIn=False)
        assert seat_msg(lis, lambda m: m.get("privateNow") is True and m["listenIn"] is False) is not None
        set_policy(srv, listenIn=True, allowPrivate=False)
        assert seat_msg(lis, lambda m: m.get("privateNow") is False) is not None
        d = denied_for(lis, "seatTake")
        assert d is not None and d["need"] == "control", "a listener never takes the seat"
    audit = (srv.shared_dir / "audit.log").read_text()
    assert "seat-private con1@" in audit


def test_seat_off(srv):
    set_policy(srv, seat=False)
    con1, seat = open_ws(srv, "con1")
    con2, _ = open_ws(srv, "con2")
    with con1, con2:
        assert seat["enabled"] is False
        assert denied_for(con1, "setFrequency", value=7074000) is None
        assert denied_for(con2, "setFrequency", value=7074000) is None, "everyone with the role controls the radio"
        d = denied_for(con1, "seatTake")
        assert d is not None and "off" in d["reason"]
    set_policy(srv, seat=True)
    audit = (srv.shared_dir / "audit.log").read_text()
    for line in ("seat-take con1@", "seat-request con2@", "seat-declined con1@", "seat-grace op1@",
                 "seat-release con1@", "policy clubadmin@"):
        assert line in audit, line
