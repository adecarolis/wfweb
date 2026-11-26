"""Shared Station: the audit log in the browser, and when an account was last
here and last on the air (#162).

Its own --shared wfweb against its own mock radio, the seat off (who may key
is test_access_seat.py's).  Then:

- GET /api/v1/shared/audit: newest first, paged, filtered by kind and by
  account; for an admin with a browser session, not for a member, a visitor
  or an admin's API token;
- the account list says who is connected, when each was last seen and what
  it last transmitted on, and a restart keeps it;
- every event the server writes belongs to one of the filter's kinds and is
  in SHARED_STATION.md's table (source check, no server).
"""

from __future__ import annotations

import json
import re
import time
from datetime import datetime, timezone
from pathlib import Path

import pytest

from conftest import launch_shared_wfweb, set_cookie, _run_on_loop, _stop_wfweb
from mock_radio import MockIcomRadio

ROOT = Path(__file__).resolve().parent.parent
ADMIN_PW = "club-admin-pw-2026"
OP_PW = "operator-pw-1"
AUDIT = "/api/v1/shared/audit"


@pytest.fixture(scope="module")
def audit_mock(event_loop):
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


@pytest.fixture(scope="module")
def home(tmp_path_factory):
    return tmp_path_factory.mktemp("audit-home")


@pytest.fixture(scope="module")
def srv(audit_mock, event_loop, home):
    s = launch_shared_wfweb(audit_mock, event_loop, home)
    r = s.post("/api/v1/auth/setup", {"code": s.setup_code(), "username": "clubadmin",
                                       "password": ADMIN_PW, "callsign": "K1FM"})
    assert r.status_code == 200, r.text
    s.admin_cookie, _ = set_cookie(r)
    for name, role in (("op", "operate"), ("idle", "listen")):
        r = s.post("/api/v1/shared/users", {"username": name, "password": OP_PW, "role": role}, cookie=s.admin_cookie)
        assert r.status_code == 201, r.text
    r = s.req("PUT", "/api/v1/shared/policy", json={"seat": False}, cookie=s.admin_cookie)
    assert r.status_code == 200, r.text
    r = s.post("/api/v1/auth/login", {"username": "op", "password": OP_PW})
    assert r.status_code == 200, r.text
    s.op_cookie, _ = set_cookie(r)
    yield s
    _stop_wfweb(s.proc)


def page(srv, **params):
    r = srv.get(AUDIT, cookie=srv.admin_cookie, params=params)
    assert r.status_code == 200, r.text
    return r.json()


def accounts(srv):
    r = srv.get("/api/v1/shared/users", cookie=srv.admin_cookie)
    assert r.status_code == 200, r.text
    return {u["name"]: u for u in r.json()["users"]}, r.json()


def age(iso):
    return (datetime.now(timezone.utc) - datetime.fromisoformat(iso.replace("Z", "+00:00"))).total_seconds()


def test_the_log_newest_first(srv):
    j = page(srv)
    lines = (srv.shared_dir / "audit.log").read_text().splitlines()
    assert len(j["entries"]) == len(lines) and "next" not in j
    for entry, line in zip(j["entries"], reversed(lines)):
        stamp, event, who = line.split(" ")[:3]
        assert (entry["time"], entry["event"]) == (stamp, event)
        assert f"{entry['name']}@{entry['ip'] or '-'}" == who or who == entry["name"]
        assert entry["detail"] == " ".join(line.split(" ")[3:])
        assert entry["kind"], f"{event} is of no kind"
    assert j["entries"][-1]["event"] == "setup" and j["reached"] == j["entries"][-1]["time"]


def test_filters_and_pages(srv):
    got = page(srv, kind="admin")["entries"]
    assert {"setup", "user-created", "policy"} <= {e["event"] for e in got}
    assert all(e["kind"] == "admin" for e in got)
    got = page(srv, name="OP")["entries"]
    assert got and all(e["name"] == "op" for e in got)
    assert page(srv, name="op", kind="seat")["entries"] == []

    whole, before, calls = [], None, 0
    while True:
        j = page(srv, limit=2, **({"before": before} if before else {}))
        assert len(j["entries"]) <= 2
        whole += j["entries"]
        calls += 1
        before = j.get("next")
        if not before:
            break
    assert calls > 2 and whole == page(srv)["entries"], "the pages are the log"

    for bad in ({"before": "x"}, {"before": "-1"}, {"limit": "0"}, {"limit": "501"}, {"limit": "x"}, {"kind": "nonsense"}):
        r = srv.get(AUDIT, cookie=srv.admin_cookie, params=bad)
        assert r.status_code == 400 and r.json()["error"], (bad, r.text)
    # Past the end (the log was pruned under the page): the newest again.
    assert page(srv, before=10**12)["entries"] == page(srv)["entries"]


def test_admins_with_a_browser_session_only(srv):
    assert srv.get(AUDIT).status_code == 401
    assert srv.get(AUDIT, cookie=srv.op_cookie).status_code == 403
    r = srv.post("/api/v1/auth/tokens", {"name": "t"}, cookie=srv.admin_cookie)
    token = r.json()["token"]
    r = srv.get(AUDIT, token=token, plain=True)
    assert r.status_code == 403 and "logged-in browser" in r.json()["error"]
    r = srv.req("DELETE", AUDIT, cookie=srv.admin_cookie)
    assert r.status_code == 405, "nothing over REST changes the log"
    for path in (AUDIT + "/", AUDIT + "?limit=1"):
        assert srv.get(path, cookie=srv.op_cookie).status_code == 403
    # A visitor the policy lets in hears the radio, not who was on it.
    srv.req("PUT", "/api/v1/shared/policy", json={"anonymous": "control"}, cookie=srv.admin_cookie)
    try:
        assert srv.get(AUDIT).status_code == 403
    finally:
        srv.req("PUT", "/api/v1/shared/policy", json={"anonymous": "none"}, cookie=srv.admin_cookie)


def test_the_logs_own_names_are_not_accounts(srv):
    # "anonymous" and "rigctld" are who the log says a visitor and a local
    # program are: an account of that name would read as them.
    for name in ("rigctld", "Anonymous"):
        r = srv.post("/api/v1/shared/users", {"username": name, "password": OP_PW, "role": "listen"}, cookie=srv.admin_cookie)
        assert r.status_code == 400 and "audit log" in r.json()["error"], r.text
        r = srv.post("/api/v1/shared/users/import", {"users": [{"username": name, "password": OP_PW, "role": "listen"}]},
                     cookie=srv.admin_cookie)
        assert r.status_code == 200 and r.json()["added"] == [] and len(r.json()["skipped"]) == 1, r.text
    assert "rigctld" not in accounts(srv)[0]


def test_seen_and_connected(srv):
    users, j = accounts(srv)
    assert age(j["now"]) < 5
    assert users["idle"]["lastSeen"] == "" and users["idle"]["connected"] is False, "never logged in"
    assert users["op"]["lastTx"] == "" and age(users["op"]["lastSeen"]) < 60
    time.sleep(1.1)
    with srv.ws(cookie=srv.op_cookie) as ws:
        ws.recv(timeout=5)
        users, _ = accounts(srv)
        assert users["op"]["connected"] is True and users["clubadmin"]["connected"] is False
        connected_at = users["op"]["lastSeen"]
        assert age(connected_at) < 5
        time.sleep(1.5)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and accounts(srv)[0]["op"]["connected"]:
        time.sleep(0.1)
    op = accounts(srv)[0]["op"]
    assert op["connected"] is False and op["lastSeen"] > connected_at, "the disconnect is when it was last here"
    # What an account gets of itself says none of it.
    me = srv.get("/api/v1/auth/me", cookie=srv.op_cookie).text
    assert "lastSeen" not in me and "lastTx" not in me


def test_last_tx_and_a_restart(srv, audit_mock, event_loop, home):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        j = srv.get("/api/v1/radio/info", cookie=srv.admin_cookie).json()
        if j.get("info", j).get("connected"):
            break
        time.sleep(0.3)
    else:
        pytest.fail("rig never connected")
    with srv.ws(cookie=srv.op_cookie) as ws:
        time.sleep(1.0)
        ws.send(json.dumps({"cmd": "setPTT", "value": True}))
        time.sleep(0.5)
        ws.send(json.dumps({"cmd": "setPTT", "value": False}))
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not any(e["event"] == "key-up" for e in page(srv, kind="tx")["entries"]):
            time.sleep(0.1)
        tx = page(srv, kind="tx", name="op")["entries"]
        assert [e["event"] for e in tx] == ["key-up", "key-down"], tx
        op = accounts(srv)[0]["op"]
        assert op["lastTx"] == tx[1]["time"] and op["lastTxOn"] == tx[1]["detail"], (op, tx)
        assert accounts(srv)[0]["clubadmin"]["lastTx"] == ""
        # Stopped with the page still connected: that is when it was last here.
        time.sleep(1.1)
        stopped = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        _stop_wfweb(srv.proc)
    saved = {u["name"]: u for u in json.loads((srv.shared_dir / "users.json").read_text())["users"]}
    assert saved["op"]["lastTx"] == op["lastTx"] and saved["op"]["lastTxOn"] == op["lastTxOn"]
    assert saved["op"]["lastSeen"] >= stopped

    again = launch_shared_wfweb(audit_mock, event_loop, home)
    srv.proc = again.proc    # the fixture stops this one
    r = again.post("/api/v1/auth/login", {"username": "clubadmin", "password": ADMIN_PW})
    assert r.status_code == 200, r.text
    again.admin_cookie, _ = set_cookie(r)
    after = accounts(again)[0]["op"]
    assert (after["lastTx"], after["lastTxOn"], after["lastSeen"]) == (op["lastTx"], op["lastTxOn"], saved["op"]["lastSeen"])
    assert after["connected"] is False


def test_every_event_has_a_kind_and_a_line_in_the_docs():
    events = set()
    for name in ("access.cpp", "webserver.cpp"):
        src = (ROOT / "src" / name).read_text(errors="replace")
        events |= set(re.findall(r'audit\(\s*(?:QStringLiteral\()?"([a-z-]+)"', src))
        for m in re.finditer(r'audit\(\s*\w+ \? QStringLiteral\("([a-z-]+)"\)\s*:\s*QStringLiteral\("([a-z-]+)"\)', src):
            events |= set(m.groups())
    assert {"login", "login-locked", "login-failed", "key-down", "seat-take", "qso-edit"} <= events and len(events) > 35, sorted(events)
    families = dict(re.findall(r'\{"([a-z]+)", "([a-z]+)"\}',
                               re.search(r"kAuditFamilies\[\] = \{(.*?)\n\};", (ROOT / "src/access.cpp").read_text(), re.S).group(1)))
    assert len(families) > 15
    no_kind = sorted(e for e in events if e.split("-")[0] not in families)
    assert not no_kind, f"no kind in access.cpp's kAuditFamilies: {no_kind}"
    doc = (ROOT / "SHARED_STATION.md").read_text()
    table = set(re.findall(r"`([a-z-]+)`", doc[doc.index("### The audit log's events"):]))
    assert not events - table, f"not in SHARED_STATION.md's table: {sorted(events - table)}"
    # ... and the page has a button per kind.
    html = (ROOT / "resources/web/index.html").read_text()
    kinds = set(re.findall(r"\['([a-z]*)', '[A-Z]+'\]", re.search(r"AUDIT_KINDS = (\[.*?\]);", html).group(1)))
    assert kinds - {""} == set(families.values())
