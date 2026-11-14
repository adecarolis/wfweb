"""Shared Station, phase 4: reservations -- who has the seat when.

Its own --shared wfweb and mock radio.  Accounts: the admin, op1 (operate),
con1 (control), lis (listen).  Then:

- the schedule over REST: everyone with an account reads it, admins write
  it, a listener cannot be given a slot, overlaps are refused with the
  slot in the way (409), back to back is fine;
- the seat follows a slot that is running: the owner takes it at will, a
  free seat waits for the owner at first, somebody else's seat passes to
  the owner at the start when they are connected, and the owner's seat is
  never handed on unasked while the slot runs;
- a weekly slot started a week ago is running now;
- removing the account removes its slots;
- the audit log.
"""

from __future__ import annotations

import json
import time
from datetime import datetime, timedelta, timezone

import pytest

from conftest import launch_shared_wfweb, set_cookie, _run_on_loop, _stop_wfweb
from mock_radio import MockIcomRadio

PASSWORDS = {"clubadmin": "club-admin-pw-2026", "op1": "operator-pw-one", "con1": "control-pw-one", "lis": "listener-pw-1"}
ROLES = {"op1": "operate", "con1": "control", "lis": "listen"}


def iso(dt):
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def now():
    return datetime.now(timezone.utc).replace(microsecond=0)


@pytest.fixture(scope="module")
def res_mock(event_loop):
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


@pytest.fixture(scope="module")
def srv(res_mock, event_loop, tmp_path_factory):
    s = launch_shared_wfweb(res_mock, event_loop, tmp_path_factory.mktemp("res-home"))
    r = s.post("/api/v1/auth/setup", {"code": s.setup_code(), "username": "clubadmin",
                                       "password": PASSWORDS["clubadmin"], "callsign": "K1FM"})
    assert r.status_code == 200, r.text
    s.admin_cookie, _ = set_cookie(r)
    s.cookies = {"clubadmin": s.admin_cookie}
    s.ids = {}
    for name, role in ROLES.items():
        r = s.post("/api/v1/shared/users", {"username": name, "password": PASSWORDS[name], "role": role}, cookie=s.admin_cookie)
        assert r.status_code == 201, r.text
        s.ids[name] = r.json()["user"]["id"]
        r = s.post("/api/v1/auth/login", {"username": name, "password": PASSWORDS[name]})
        assert r.status_code == 200, r.text
        s.cookies[name], _ = set_cookie(r)
    # Requests are granted after 2 s when unanswered, so the owner's
    # protection during a slot is visible within a test.
    r = s.req("PUT", "/api/v1/shared/policy", json={"seatRequestSecs": 2}, cookie=s.admin_cookie)
    assert r.status_code == 200, r.text
    yield s
    _stop_wfweb(s.proc)


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
    return recv_until(ws, lambda m: m.get("type") == "seat" and pred(m), timeout)


def drain(ws, seconds=0.3):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            ws.recv(timeout=max(0.05, deadline - time.monotonic()))
        except TimeoutError:
            break


def send(ws, cmd, **fields):
    fields["cmd"] = cmd
    ws.send(json.dumps(fields))


def denied_for(ws, cmd, timeout=0.8, **fields):
    send(ws, cmd, **fields)
    return recv_until(ws, lambda m: m.get("type") == "denied" and m.get("cmd") == cmd, timeout)


def open_ws(srv, name):
    ws = srv.ws(cookie=srv.cookies[name])
    assert recv_until(ws, lambda m: m.get("type") == "access") is not None
    seat = seat_msg(ws)
    assert seat is not None
    return ws, seat


def reserve(srv, who, start, minutes=5, repeat="once", **extra):
    body = dict(username=who, start=iso(start), minutes=minutes, repeat=repeat, **extra)
    return srv.post("/api/v1/shared/reservations", body, cookie=srv.admin_cookie)


def unreserve(srv, rid):
    return srv.req("DELETE", f"/api/v1/shared/reservations/{rid}", cookie=srv.admin_cookie)


def schedule(srv, who="clubadmin"):
    r = srv.get("/api/v1/shared/reservations", cookie=srv.cookies[who])
    assert r.status_code == 200, r.text
    return r.json()


@pytest.fixture(autouse=True)
def clean_slate(srv):
    """No slots and a free seat around every test."""
    for r in schedule(srv)["reservations"]:
        unreserve(srv, r["id"])
    srv.req("DELETE", "/api/v1/shared/seat", cookie=srv.admin_cookie)
    yield
    for r in schedule(srv)["reservations"]:
        unreserve(srv, r["id"])
    srv.req("DELETE", "/api/v1/shared/seat", cookie=srv.admin_cookie)


# ---------------------------------------------------------------------------
# REST
# ---------------------------------------------------------------------------


def test_schedule_rest(srv):
    sched = schedule(srv, "lis")
    assert sched == {"reservations": [], "upcoming": [], "active": None}, "a listener reads the schedule"

    start = now() + timedelta(hours=2)
    r = srv.post("/api/v1/shared/reservations", {"username": "op1", "start": iso(start), "minutes": 60}, cookie=srv.cookies["con1"])
    assert r.status_code == 403, "only admins write"
    assert reserve(srv, "nobody", start).status_code == 400
    assert reserve(srv, "lis", start).status_code == 400, "a listener cannot hold the seat"
    assert reserve(srv, "op1", start, minutes=2).status_code == 400
    assert reserve(srv, "op1", start, repeat="monthly").status_code == 400
    r = srv.post("/api/v1/shared/reservations", {"username": "op1", "start": "tomorrow", "minutes": 60}, cookie=srv.admin_cookie)
    assert r.status_code == 400

    r = reserve(srv, "op1", start, minutes=60, title="net control")
    assert r.status_code == 201, r.text
    first = r.json()["reservation"]
    assert first["name"] == "op1" and first["minutes"] == 60 and first["repeat"] == "once" and first["title"] == "net control"

    r = reserve(srv, "con1", start + timedelta(minutes=30), minutes=30)
    assert r.status_code == 409 and r.json()["conflict"].startswith(first["id"]), r.text
    r = reserve(srv, "con1", start + timedelta(minutes=60), minutes=30)
    assert r.status_code == 201, "back to back is fine"
    second = r.json()["reservation"]
    r = reserve(srv, "con1", start - timedelta(days=7), minutes=120, repeat="weekly")
    assert r.status_code == 409, "a weekly slot hitting a one-off is refused"
    r = reserve(srv, "con1", start + timedelta(hours=3), minutes=30, repeat="weekly", until=iso(start + timedelta(days=30)))
    assert r.status_code == 201, r.text
    weekly = r.json()["reservation"]
    assert weekly["repeat"] == "weekly" and weekly["until"]

    sched = schedule(srv, "op1")
    assert sorted(x["id"] for x in sched["reservations"]) == sorted([first["id"], second["id"], weekly["id"]])
    assert [x["id"] for x in sched["upcoming"][:2]] == [first["id"], second["id"]], "soonest first"
    weekly_runs = [x for x in sched["upcoming"] if x["id"] == weekly["id"]]
    assert len(weekly_runs) == 2, "two weekly runs in the next fortnight"
    assert sched["active"] is None

    assert unreserve(srv, first["id"]).status_code == 200
    assert unreserve(srv, first["id"]).status_code == 404
    assert srv.req("DELETE", f"/api/v1/shared/reservations/{second['id']}", cookie=srv.cookies["op1"]).status_code == 403


# ---------------------------------------------------------------------------
# The seat follows the slot
# ---------------------------------------------------------------------------


def test_running_slot_owner_priority(srv):
    # The slot starts with its owner away: the free seat waits for them.
    con1, _ = open_ws(srv, "con1")
    with con1:
        r = reserve(srv, "op1", now() - timedelta(seconds=30), minutes=5, title="now")
        assert r.status_code == 201, r.text
        seat = seat_msg(con1, lambda m: m.get("reservation") is not None)
        assert seat is not None and seat["reservation"]["name"] == "op1" and seat["reservation"]["mine"] is False
        time.sleep(1.5)   # a tick: the owner is not here, nothing changes hands
        drain(con1)
        d = denied_for(con1, "seatTake")
        assert d is not None and "reserved for op1" in d["reason"]

        # The owner arrives and takes it at will.
        op1, mine = open_ws(srv, "op1")
        with op1:
            assert mine["reservation"]["mine"] is True and mine["holder"] is None
            send(op1, "seatTake")
            assert seat_msg(op1, lambda m: m["mine"]) is not None
            # A request from somebody else is not granted while the slot
            # runs, even unanswered past the 2 s policy.
            drain(con1)
            send(con1, "seatTake")
            assert seat_msg(con1, lambda m: m.get("requested") is not None) is not None
            assert seat_msg(con1, lambda m: m["mine"], timeout=4.0) is None, "the owner keeps the seat during the slot"
            # The slot removed: the pending request is granted at the next tick.
            sched = schedule(srv)
            assert unreserve(srv, sched["reservations"][0]["id"]).status_code == 200
            assert seat_msg(con1, lambda m: m["mine"], timeout=5.0) is not None, "no slot, the usual rules"


def test_slot_start_hands_the_seat_over(srv):
    con1, _ = open_ws(srv, "con1")
    op1, _ = open_ws(srv, "op1")
    with con1, op1:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        drain(op1)
        r = reserve(srv, "op1", now() + timedelta(seconds=3), minutes=5)
        assert r.status_code == 201, r.text
        got = seat_msg(op1, lambda m: m["mine"], timeout=8.0)
        assert got is not None and got["reservation"]["mine"] is True, "the owner, connected, is given the seat at the start"
        lost = recv_until(con1, lambda m: m.get("type") == "seatLost", 2.0)
        assert lost is not None and "reserved by op1" in lost["reason"]
        # The owner may take it back from anyone during the slot.
        send(op1, "seatRelease")
        assert seat_msg(op1, lambda m: m["holder"] is None) is not None
        drain(con1)
        send(con1, "seatTake")   # inside the hold window: refused
        d = recv_until(con1, lambda m: m.get("type") == "denied" and m.get("cmd") == "seatTake", 2.0)
        assert d is not None and "reserved for op1" in d["reason"]
        admin, _ = open_ws(srv, "clubadmin")
        with admin:
            send(admin, "seatTake")
            assert seat_msg(admin, lambda m: m["mine"]) is not None, "an admin is not held off"
            drain(op1)
            send(op1, "seatTake")
            assert seat_msg(op1, lambda m: m["mine"]) is not None, "the owner takes it back at will"
            lost = recv_until(admin, lambda m: m.get("type") == "seatLost", 2.0)
            assert lost is not None and "reserved slot" in lost["reason"]
    assert "seat-reserved op1@" in (srv.shared_dir / "audit.log").read_text()


def test_slot_start_without_the_owner_tells_the_holder(srv):
    con1, _ = open_ws(srv, "con1")
    with con1:
        send(con1, "seatTake")
        assert seat_msg(con1, lambda m: m["mine"]) is not None
        r = reserve(srv, "op1", now() + timedelta(seconds=2), minutes=5)
        assert r.status_code == 201, r.text
        # First the five-minute warning (the slot is within five minutes),
        # then, at the start, the notice that the owner may take it.
        warn = recv_until(con1, lambda m: m.get("type") == "seatNotice" and "reserved from" in m["text"], 6.0)
        assert warn is not None and "op1" in warn["text"]
        note = recv_until(con1, lambda m: m.get("type") == "seatNotice" and "take it on arriving" in m["text"], 8.0)
        assert note is not None and "op1" in note["text"]
        assert seat_msg(con1, lambda m: m.get("reservation") is not None and m["mine"] is True, 4.0) is not None, "the holder keeps the seat until the owner comes"


def test_weekly_slot_runs_now(srv):
    r = reserve(srv, "op1", now() - timedelta(days=7, seconds=20), minutes=5, repeat="weekly", title="weekly net")
    assert r.status_code == 201, r.text
    sched = schedule(srv)
    assert sched["active"] is not None and sched["active"]["name"] == "op1" and sched["active"]["title"] == "weekly net"
    op1, seat = open_ws(srv, "op1")
    with op1:
        assert seat["reservation"]["mine"] is True and seat["reservation"]["repeat"] == "weekly"
    runs = [x for x in sched["upcoming"] if x["id"] == r.json()["reservation"]["id"]]
    assert len(runs) == 3, "the run now and the next two weeks"


def test_removing_the_account_removes_its_slots(srv):
    r = srv.post("/api/v1/shared/users", {"username": "tmp", "password": "temporary-pw", "role": "control"}, cookie=srv.admin_cookie)
    assert r.status_code == 201
    tmp_id = r.json()["user"]["id"]
    assert reserve(srv, "tmp", now() + timedelta(days=1), minutes=30).status_code == 201
    assert len(schedule(srv)["reservations"]) == 1
    assert srv.req("DELETE", f"/api/v1/shared/users/{tmp_id}", cookie=srv.admin_cookie).status_code == 200
    assert schedule(srv)["reservations"] == []
    audit = (srv.shared_dir / "audit.log").read_text()
    assert "reservation-add clubadmin@127.0.0.1 tmp" in audit
    assert "reservation-remove clubadmin@" in audit
