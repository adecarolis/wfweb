"""Shared Station (--shared): accounts and the gate in front of the server.

A second wfweb is started with --shared against a mock radio of its own, with
no accounts: the first-run setup code must appear in its log, the setup must
turn it into the admin's session cookie, and from then on the login routes,
the WebSocket gate (authRequired + close 4401 without a session, the cookie
on the upgrade with one), API tokens on the plain REST port, account
management and the files on disk are exercised through the real HTTP layer.
The lockout test runs last: five misses lock this address for a minute.

The standard wfweb_instance (no --shared) checks that nothing changed for
an open server.
"""

from __future__ import annotations

import json
import os
import re
import ssl
import stat
import subprocess
import sys
import time

import pytest
import requests

from conftest import launch_shared_wfweb, set_cookie, _run_on_loop, _stop_wfweb
from mock_radio import MockIcomRadio


@pytest.fixture(scope="module")
def shared_mock(event_loop):
    """A mock of its own: wfweb_instance holds the session-wide one."""
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


@pytest.fixture(scope="module")
def shared(shared_mock, event_loop, tmp_path_factory):
    srv = launch_shared_wfweb(shared_mock, event_loop, tmp_path_factory.mktemp("shared-home"))
    yield srv
    _stop_wfweb(srv.proc)


# ---------------------------------------------------------------------------
# Before any account exists
# ---------------------------------------------------------------------------


def test_me_before_setup(shared):
    me = shared.get("/api/v1/auth/me").json()
    assert me["shared"] is True
    assert me["authenticated"] is False
    assert me["setupRequired"] is True
    assert me["secure"] is True


def test_api_needs_a_session_but_the_page_is_public(shared):
    r = shared.get("/api/v1/radio/info")
    assert r.status_code == 401
    assert "Bearer" in r.headers.get("WWW-Authenticate", "")
    r = shared.get("/api/v1/radio/info", plain=True)
    assert r.status_code == 401, "the plain REST port is gated too"
    r = shared.get("/")
    assert r.status_code == 200 and b"loginOverlay" in r.content, "the SPA loads: it holds the login sheet"
    r = shared.get("/api/v1/auth/me", plain=True)
    assert r.status_code == 200 and r.json()["secure"] is False


def test_ws_without_session_is_refused(shared):
    pytest.importorskip("websockets")
    from websockets.exceptions import ConnectionClosed
    with shared.ws() as ws:
        first = json.loads(ws.recv(timeout=10))
        assert first["type"] == "authRequired"
        assert first["setupRequired"] is True
        with pytest.raises(ConnectionClosed) as info:
            ws.recv(timeout=10)
        assert info.value.rcvd.code == 4401


def test_setup_creates_the_admin(shared):
    code = shared.setup_code()
    body = {"code": code, "username": "clubadmin", "password": shared.admin_password, "callsign": "k1fm"}

    r = shared.post("/api/v1/auth/setup", dict(body, code="AAAA-AAAA"))
    assert r.status_code == 403 and "code" in r.json()["error"]
    r = shared.post("/api/v1/auth/setup", dict(body, password="short"))
    assert r.status_code == 400 and "8" in r.json()["error"]
    r = shared.post("/api/v1/auth/setup", dict(body, username="x y"))
    assert r.status_code == 400
    r = shared.post("/api/v1/auth/setup", body, plain=True)
    assert r.status_code == 403, "no setup over plain HTTP"

    r = shared.post("/api/v1/auth/setup", body)
    assert r.status_code == 200, r.text
    assert r.json()["authenticated"] is True
    assert r.json()["user"]["role"] == "admin"
    assert r.json()["user"]["callsign"] == "K1FM"
    token, attrs = set_cookie(r)
    assert len(token) == 43
    assert "HttpOnly" in attrs and "SameSite=Strict" in attrs and "Secure" in attrs
    shared.admin_cookie = token

    me = shared.get("/api/v1/auth/me", cookie=token).json()
    assert me["authenticated"] is True and me["user"]["name"] == "clubadmin"
    assert me["setupRequired"] is False

    r = shared.post("/api/v1/auth/setup", body)
    assert r.status_code == 403, "setup is a one-time door"


# ---------------------------------------------------------------------------
# Sessions
# ---------------------------------------------------------------------------


def test_login_and_logout(shared):
    r = shared.post("/api/v1/auth/login", {"username": "clubadmin", "password": "nope-nope-nope"})
    assert r.status_code == 401
    r = shared.post("/api/v1/auth/login", {"username": "clubadmin", "password": shared.admin_password}, plain=True)
    assert r.status_code == 403, "passwords do not travel in clear"
    r = shared.post("/api/v1/auth/login", {"username": "CLUBADMIN", "password": shared.admin_password})
    assert r.status_code == 200, r.text
    cookie, _ = set_cookie(r)

    r = shared.get("/api/v1/radio/info", cookie=cookie)
    assert r.status_code == 200, "the API opens up with a session"

    r = shared.post("/api/v1/auth/logout", cookie=cookie)
    assert r.status_code == 200
    _, attrs = set_cookie(r)
    assert "Max-Age=0" in attrs
    assert shared.get("/api/v1/auth/me", cookie=cookie).json()["authenticated"] is False
    assert shared.get("/api/v1/radio/info", cookie=cookie).status_code == 401


def test_ws_with_cookie_and_logout_closes_it(shared):
    pytest.importorskip("websockets")
    from websockets.exceptions import ConnectionClosed
    r = shared.post("/api/v1/auth/login", {"username": "clubadmin", "password": shared.admin_password})
    cookie, _ = set_cookie(r)
    with shared.ws(cookie=cookie) as ws:
        first = json.loads(ws.recv(timeout=10))
        assert first["type"] == "access"
        assert first["shared"] is True
        assert first["user"]["name"] == "clubadmin" and first["user"]["role"] == "admin"
        second = json.loads(ws.recv(timeout=10))
        assert second["type"] == "seat", "the seat follows the identity"
        third = json.loads(ws.recv(timeout=10))
        assert third["type"] == "rigInfo", "then the state"

        # Logging the session out elsewhere ends this socket too.
        assert shared.post("/api/v1/auth/logout", cookie=cookie).status_code == 200
        deadline = time.monotonic() + 10
        closed = None
        while time.monotonic() < deadline:
            try:
                msg = json.loads(ws.recv(timeout=5))
            except ConnectionClosed as e:
                closed = e.rcvd.code
                break
            if msg.get("type") == "authRequired":
                continue
        assert closed == 4401


def test_session_survives_a_restart_is_in_the_unit_test(shared):
    # tests/access_test.cpp covers sessions.json across a reload; here only
    # that the file exists and carries no token in clear.
    with open(shared.shared_dir / "sessions.json") as f:
        data = json.load(f)
    assert data["version"] == 1
    for s in data["sessions"]:
        assert len(s["hash"]) == 64


# ---------------------------------------------------------------------------
# API tokens
# ---------------------------------------------------------------------------


def test_api_tokens(shared):
    cookie = shared.admin_cookie
    r = shared.post("/api/v1/auth/tokens", {"name": "logger script"}, cookie=cookie)
    assert r.status_code == 201, r.text
    tok = r.json()
    assert tok["name"] == "logger script" and len(tok["token"]) == 43

    r = shared.get("/api/v1/radio/info", token=tok["token"], plain=True)
    assert r.status_code == 200, "a bearer token opens the plain REST port"
    r = shared.get("/api/v1/auth/me", token=tok["token"], plain=True).json()
    assert r["authenticated"] is True and r["user"]["name"] == "clubadmin"

    listed = shared.get("/api/v1/auth/tokens", cookie=cookie).json()["tokens"]
    assert [t["id"] for t in listed] == [tok["id"]]
    assert "token" not in listed[0] and "hash" not in listed[0]

    r = shared.post("/api/v1/auth/tokens", {"name": "more"}, token=tok["token"])
    assert r.status_code == 403, "a token cannot mint tokens"

    r = shared.req("DELETE", f"/api/v1/auth/tokens/{tok['id']}", cookie=cookie)
    assert r.status_code == 200
    assert shared.get("/api/v1/radio/info", token=tok["token"], plain=True).status_code == 401
    assert shared.get("/api/v1/radio/info", token="not-a-token", plain=True).status_code == 401


# ---------------------------------------------------------------------------
# Accounts (admin only)
# ---------------------------------------------------------------------------


def test_user_management(shared):
    admin = shared.admin_cookie
    users = shared.get("/api/v1/shared/users", cookie=admin).json()["users"]
    assert [u["name"] for u in users] == ["clubadmin"]
    assert "passwordHash" not in users[0]

    r = shared.post("/api/v1/shared/users", {"username": "op", "password": "operator-pw", "role": "operate", "callsign": "w1aw"}, cookie=admin)
    assert r.status_code == 201, r.text
    op = r.json()["user"]
    assert op["role"] == "operate" and op["callsign"] == "W1AW"
    r = shared.post("/api/v1/shared/users", {"username": "op", "password": "operator-pw", "role": "listen"}, cookie=admin)
    assert r.status_code == 409
    r = shared.post("/api/v1/shared/users", {"username": "x", "password": "operator-pw", "role": "god"}, cookie=admin)
    assert r.status_code == 400

    r = shared.post("/api/v1/auth/login", {"username": "op", "password": "operator-pw"})
    assert r.status_code == 200
    op_cookie, _ = set_cookie(r)
    assert shared.get("/api/v1/shared/users", cookie=op_cookie).status_code == 403, "not an admin"
    assert shared.get("/api/v1/radio/info", cookie=op_cookie).status_code == 200

    # Notes: the admins' words about an account.  The account never sees them.
    assert op["notes"] == ""
    r = shared.req("PUT", f"/api/v1/shared/users/{op['id']}", json={"notes": "  Discord 1234567890, member #42 "}, cookie=admin)
    assert r.status_code == 200 and r.json()["user"]["notes"] == "Discord 1234567890, member #42"
    listed = [u for u in shared.get("/api/v1/shared/users", cookie=admin).json()["users"] if u["id"] == op["id"]][0]
    assert listed["notes"] == "Discord 1234567890, member #42"
    assert "Discord" not in shared.get("/api/v1/auth/me", cookie=op_cookie).text
    r = shared.post("/api/v1/shared/users", {"username": "noted", "password": "operator-pw", "role": "listen", "notes": "guest of W1AW"}, cookie=admin)
    assert r.status_code == 201 and r.json()["user"]["notes"] == "guest of W1AW"
    assert shared.req("DELETE", f"/api/v1/shared/users/{r.json()['user']['id']}", cookie=admin).status_code == 200
    audit = (shared.shared_dir / "audit.log").read_text()
    assert "user-updated clubadmin@" in audit and "Discord" not in audit, "the audit log says notes changed, not what they say"

    # The user list as a file: every account with its hash, nothing of the
    # server's own (ids, sessions, tokens).  Admins only.
    assert shared.get("/api/v1/shared/users/export", cookie=op_cookie).status_code == 403
    exp = shared.get("/api/v1/shared/users/export", cookie=admin).json()
    assert exp["wfwebUsers"] == 1
    mine = [u for u in exp["users"] if u["username"] == "op"][0]
    assert set(mine) == {"username", "callsign", "role", "onAir", "notes", "disabled", "passwordHash"}
    assert mine["passwordHash"].startswith("pbkdf2-sha256$") and mine["notes"] == "Discord 1234567890, member #42"
    # Imported: an entry with the hash logs in with the old password, one with
    # a plain password is hashed here; names that exist are left alone.
    carried = dict(mine, username="carried", notes="from the other station", callsign="k2def", onAir="own")
    r = shared.post("/api/v1/shared/users/import", {"users": [
        carried,
        {"username": "fresh", "password": "fresh-password", "role": "listen"},
        dict(mine, role="admin", notes="overwritten?"),
        {"username": "nopass", "role": "listen"},
        {"username": "badrole", "password": "fresh-password", "role": "owner"},
        {"username": "short", "password": "x", "role": "listen"},
        {"username": "forged", "passwordHash": "plain-text", "role": "listen"},
    ]}, cookie=admin)
    assert r.status_code == 200, r.text
    assert r.json()["added"] == ["carried", "fresh"]
    assert [k["username"] for k in r.json()["skipped"]] == ["op", "nopass", "badrole", "short", "forged"]
    assert "taken" in r.json()["skipped"][0]["reason"]
    byname = {u["name"]: u for u in shared.get("/api/v1/shared/users", cookie=admin).json()["users"]}
    assert byname["op"]["role"] == "operate" and byname["op"]["notes"] == "Discord 1234567890, member #42"
    assert byname["carried"]["callsign"] == "K2DEF" and byname["carried"]["onAir"] == "own"
    assert byname["carried"]["notes"] == "from the other station" and byname["carried"]["id"] != op["id"]
    assert shared.post("/api/v1/auth/login", {"username": "carried", "password": "operator-pw"}).status_code == 200
    assert shared.post("/api/v1/auth/login", {"username": "fresh", "password": "fresh-password"}).status_code == 200
    assert shared.post("/api/v1/shared/users/import", {"users": []}, cookie=admin).status_code == 400
    assert shared.post("/api/v1/shared/users/import", {"users": [carried]}, cookie=op_cookie).status_code == 403
    for name in ("carried", "fresh"):
        assert shared.req("DELETE", f"/api/v1/shared/users/{byname[name]['id']}", cookie=admin).status_code == 200
    audit = (shared.shared_dir / "audit.log").read_text()
    assert "users-export clubadmin@" in audit and "users-import clubadmin@" in audit and "2 added, 5 skipped" in audit

    # Disabling ends the sessions and refuses the login.
    r = shared.req("PUT", f"/api/v1/shared/users/{op['id']}", json={"disabled": True}, cookie=admin)
    assert r.status_code == 200 and r.json()["user"]["disabled"] is True
    assert shared.get("/api/v1/auth/me", cookie=op_cookie).json()["authenticated"] is False
    r = shared.post("/api/v1/auth/login", {"username": "op", "password": "operator-pw"})
    assert r.status_code == 403 and "disabled" in r.json()["error"]
    r = shared.req("PUT", f"/api/v1/shared/users/{op['id']}", json={"disabled": False, "role": "control"}, cookie=admin)
    assert r.status_code == 200 and r.json()["user"]["role"] == "control"

    # An admin-set password ends the sessions too.
    r = shared.post("/api/v1/auth/login", {"username": "op", "password": "operator-pw"})
    op_cookie, _ = set_cookie(r)
    r = shared.req("PUT", f"/api/v1/shared/users/{op['id']}", json={"password": "new-op-password"}, cookie=admin)
    assert r.status_code == 200
    assert shared.get("/api/v1/auth/me", cookie=op_cookie).json()["authenticated"] is False
    assert shared.post("/api/v1/auth/login", {"username": "op", "password": "new-op-password"}).status_code == 200

    # The last admin stays an admin.
    admin_id = users[0]["id"]
    assert shared.req("PUT", f"/api/v1/shared/users/{admin_id}", json={"role": "listen"}, cookie=admin).status_code == 409
    assert shared.req("PUT", f"/api/v1/shared/users/{admin_id}", json={"disabled": True}, cookie=admin).status_code == 409
    assert shared.req("DELETE", f"/api/v1/shared/users/{admin_id}", cookie=admin).status_code == 409

    r = shared.req("DELETE", f"/api/v1/shared/users/{op['id']}", cookie=admin)
    assert r.status_code == 200
    assert [u["name"] for u in r.json()["users"]] == ["clubadmin"]
    assert shared.req("DELETE", f"/api/v1/shared/users/{op['id']}", cookie=admin).status_code == 404


def test_account_routes_hardening(shared):
    admin = shared.admin_cookie
    r = shared.post("/api/v1/shared/users", {"username": "hard", "password": "hardening-pw", "role": "operate", "callsign": "w1hrd"}, cookie=admin)
    assert r.status_code == 201, r.text
    uid = r.json()["user"]["id"]

    def user():
        return [u for u in shared.get("/api/v1/shared/users", cookie=admin).json()["users"] if u["id"] == uid][0]

    # Everything is checked before anything is changed.
    r = shared.req("PUT", f"/api/v1/shared/users/{uid}", json={"role": "listen", "callsign": "??"}, cookie=admin)
    assert r.status_code == 400 and user()["role"] == "operate", "a bad field leaves the good one unapplied"
    r = shared.req("PUT", f"/api/v1/shared/users/{uid}", json={"callsign": "", "onAir": "own"}, cookie=admin)
    assert r.status_code == 400 and user()["callsign"] == "W1HRD"
    for body in ({"disabled": "true"}, {"password": ""}, {"password": "short"}, {"kick": "yes"}, {"notes": 5}):
        assert shared.req("PUT", f"/api/v1/shared/users/{uid}", json=body, cookie=admin).status_code == 400, body
    assert user()["disabled"] is False
    # A callsign is checked when the account is made, too.
    r = shared.post("/api/v1/shared/users", {"username": "badcall", "password": "hardening-pw", "role": "listen", "callsign": "W1 <b>"}, cookie=admin)
    assert r.status_code == 400 and "callsign" in r.json()["error"], r.text

    # An admin's API token does not manage accounts; it still reads the policy.
    tok = shared.post("/api/v1/auth/tokens", {"name": "script"}, cookie=admin).json()
    for method, path, body in (("GET", "/api/v1/shared/users", None), ("GET", "/api/v1/shared/users/export", None),
                               ("POST", "/api/v1/shared/users", {"username": "viatoken", "password": "token-made-pw", "role": "admin"}),
                               ("PUT", f"/api/v1/shared/users/{uid}", {"role": "admin"}),
                               ("DELETE", f"/api/v1/shared/users/{uid}", None),
                               ("POST", "/api/v1/shared/users/import", {"users": [{"username": "x1", "password": "token-made-pw", "role": "admin"}]})):
        r = shared.req(method, path, token=tok["token"], json=body) if body is not None else shared.req(method, path, token=tok["token"])
        assert r.status_code == 403 and "logged-in browser" in r.json()["error"], f"{method} {path}: {r.status_code} {r.text}"
    assert shared.get("/api/v1/shared/policy", token=tok["token"]).status_code == 200
    assert user()["role"] == "operate"
    # Nor does a token change a password.
    r = shared.post("/api/v1/auth/password", {"current": shared.admin_password, "password": "by-token-never"}, token=tok["token"])
    assert r.status_code == 403 and "logged-in browser" in r.json()["error"]
    assert shared.req("DELETE", f"/api/v1/auth/tokens/{tok['id']}", cookie=admin).status_code == 200

    # A password set by an admin ends the account's API tokens with its sessions.
    r = shared.post("/api/v1/auth/login", {"username": "hard", "password": "hardening-pw"})
    hard_a, _ = set_cookie(r)
    hard_tok = shared.post("/api/v1/auth/tokens", {"name": "mine"}, cookie=hard_a).json()["token"]
    assert shared.get("/api/v1/radio/info", token=hard_tok).status_code == 200
    assert shared.req("PUT", f"/api/v1/shared/users/{uid}", json={"password": "set-by-the-admin"}, cookie=admin).status_code == 200
    assert shared.get("/api/v1/radio/info", token=hard_tok).status_code == 401
    assert shared.get("/api/v1/auth/me", cookie=hard_a).json()["authenticated"] is False

    # One's own password change keeps this browser and ends the others.
    hard_a, _ = set_cookie(shared.post("/api/v1/auth/login", {"username": "hard", "password": "set-by-the-admin"}))
    hard_b, _ = set_cookie(shared.post("/api/v1/auth/login", {"username": "hard", "password": "set-by-the-admin"}))
    r = shared.post("/api/v1/auth/password", {"current": "set-by-the-admin", "password": "chosen-by-hard-1"}, cookie=hard_a)
    assert r.status_code == 200, r.text
    assert shared.get("/api/v1/auth/me", cookie=hard_a).json()["authenticated"] is True
    assert shared.get("/api/v1/auth/me", cookie=hard_b).json()["authenticated"] is False, "the other session is ended"
    # The current password is a guess like any other: counted and audited.
    for _ in range(5):
        assert shared.post("/api/v1/auth/password", {"current": "nope-nope-nope", "password": "another-pw-here"}, cookie=hard_a).status_code == 403
    r = shared.post("/api/v1/auth/password", {"current": "chosen-by-hard-1", "password": "another-pw-here"}, cookie=hard_a)
    assert r.status_code == 429, "five misses lock the password change"
    audit = (shared.shared_dir / "audit.log").read_text()
    assert "password-change-failed hard@" in audit

    # A login name no account can have: refused, and not a word of it in
    # the audit log (a line break in it forged a line).
    r = shared.post("/api/v1/auth/login", {"username": "x\n2026-01-01T00:00:00Z login clubadmin@6.6.6.6 admin", "password": "p"})
    assert r.status_code == 401
    # A name under the body cap is read, refused and not written whole;
    # one over it is refused from the headers.
    r = shared.post("/api/v1/auth/login", {"username": "A" * 60000, "password": "p"})
    assert r.status_code == 401
    try:
        r = shared.post("/api/v1/auth/login", {"username": "A" * 200000, "password": "p"})
        assert r.status_code in (401, 413)
    except requests.ConnectionError:
        pass    # answered 413 and closed before the body was all sent: as good
    audit = (shared.shared_dir / "audit.log").read_text()
    assert "clubadmin@6.6.6.6" not in audit and "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" not in audit
    assert all(len(line) < 700 for line in audit.splitlines())

    assert shared.req("DELETE", f"/api/v1/shared/users/{uid}", cookie=admin).status_code == 200
    # (Leaves this address with no miss on its count for the tests below.)
    assert shared.post("/api/v1/auth/login", {"username": "clubadmin", "password": shared.admin_password}).status_code == 200


def test_password_change(shared):
    cookie = shared.admin_cookie
    r = shared.post("/api/v1/auth/password", {"current": "wrong-wrong", "password": "another-admin-pw"}, cookie=cookie)
    assert r.status_code == 403
    r = shared.post("/api/v1/auth/password", {"current": shared.admin_password, "password": "short"}, cookie=cookie)
    assert r.status_code == 400
    r = shared.post("/api/v1/auth/password", {"current": shared.admin_password, "password": "another-admin-pw"}, cookie=cookie)
    assert r.status_code == 200
    shared.admin_password = "another-admin-pw"
    assert shared.post("/api/v1/auth/login", {"username": "clubadmin", "password": shared.admin_password}).status_code == 200
    assert shared.get("/api/v1/auth/me", cookie=cookie).json()["authenticated"] is True, "own sessions stay"


# ---------------------------------------------------------------------------
# On disk
# ---------------------------------------------------------------------------


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX permissions")
def test_files_are_private(shared):
    for name in ("users.json", "sessions.json", "audit.log"):
        path = shared.shared_dir / name
        assert path.exists(), name
        mode = stat.S_IMODE(os.stat(path).st_mode)
        assert mode & 0o077 == 0, f"{name} is {oct(mode)}"
    users = (shared.shared_dir / "users.json").read_text()
    assert "pbkdf2-sha256$" in users
    assert shared.admin_password not in users and "operator-pw" not in users
    audit = (shared.shared_dir / "audit.log").read_text()
    for event in ("setup clubadmin@", "login clubadmin@", "logout clubadmin@", "user-created clubadmin@",
                  "user-removed clubadmin@", "token-created clubadmin@", "password-changed clubadmin@"):
        assert event in audit, event


# ---------------------------------------------------------------------------
# A lost admin password: --shared-reset-admin
# ---------------------------------------------------------------------------


def test_reset_admin_reaches_the_page(event_loop, tmp_path):
    """The flag brings the setup code back; the page must be told, or its
    sheet has no field for the code (2026-11-14: with visitors let in, and
    with them refused, it only ever offered the plain login)."""
    pytest.importorskip("websockets")
    mock = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, mock.start())
    try:
        srv = launch_shared_wfweb(mock, event_loop, tmp_path)
        r = srv.post("/api/v1/auth/setup", {"code": srv.setup_code(), "username": "clubadmin",
                                            "password": "the-forgotten-one"})
        assert r.status_code == 200, r.text
        _stop_wfweb(srv.proc)

        srv = launch_shared_wfweb(mock, event_loop, tmp_path, extra_args=("--shared-reset-admin",))
        try:
            with srv.ws() as ws:
                first = json.loads(ws.recv(timeout=10))
                assert first["type"] == "authRequired"
                assert first["setupRequired"] is False and first["recovery"] is True
            # A visitor who is let in learns it from the access message.
            old = set_cookie(srv.post("/api/v1/auth/login", {"username": "clubadmin", "password": "the-forgotten-one"}))[0]
            assert srv.req("PUT", "/api/v1/shared/policy", json={"anonymous": "listen"}, cookie=old).status_code == 200
            with srv.ws() as ws:
                first = json.loads(ws.recv(timeout=10))
                assert first["type"] == "access" and first["anonymous"] is True
                assert first.get("recovery") is True and "setupRequired" not in first
            with srv.ws(cookie=old) as ws:
                first = json.loads(ws.recv(timeout=10))
                assert first["type"] == "access" and "recovery" not in first, "only a visitor is told"

            r = srv.post("/api/v1/auth/setup", {"code": srv.setup_code(), "username": "clubadmin",
                                                "password": "a-new-password-2026"})
            assert r.status_code == 200, r.text
            assert srv.post("/api/v1/auth/login", {"username": "clubadmin", "password": "the-forgotten-one"}).status_code == 401
            assert srv.post("/api/v1/auth/login", {"username": "clubadmin", "password": "a-new-password-2026"}).status_code == 200
            with srv.ws() as ws:
                first = json.loads(ws.recv(timeout=10))
                assert first["type"] == "access" and "recovery" not in first, "the code is spent"
        finally:
            _stop_wfweb(srv.proc)
    finally:
        _run_on_loop(event_loop, mock.stop())


# ---------------------------------------------------------------------------
# An open server is unchanged
# ---------------------------------------------------------------------------


def test_open_server_has_no_gate(rest_url, wfweb_instance):
    _, web_port = wfweb_instance
    r = requests.get(f"http://127.0.0.1:{web_port + 1}/api/v1/auth/me", timeout=5)
    assert r.status_code == 200 and r.json() == {"shared": False}
    assert requests.get(rest_url + "/info", timeout=5).status_code == 200
    r = requests.post(f"http://127.0.0.1:{web_port + 1}/api/v1/auth/login", json={"username": "a", "password": "b"}, timeout=5)
    assert r.status_code == 404


# ---------------------------------------------------------------------------
# Last: lockout (locks 127.0.0.1 for a minute)
# ---------------------------------------------------------------------------


def test_lockout_after_five_misses(shared):
    for _ in range(4):
        assert shared.post("/api/v1/auth/login", {"username": "ghost", "password": "wrong-wrong"}).status_code == 401
    r = shared.post("/api/v1/auth/login", {"username": "ghost", "password": "wrong-wrong"})
    assert r.status_code == 429
    assert int(r.headers["Retry-After"]) > 0
    assert r.json()["retryAfter"] > 0
    # The address is locked, so the right password for another account is
    # refused too, and the setup door (already shut) stays shut.
    r = shared.post("/api/v1/auth/login", {"username": "clubadmin", "password": shared.admin_password})
    assert r.status_code == 429
    audit = (shared.shared_dir / "audit.log").read_text()
    assert "login-locked ghost@" in audit
