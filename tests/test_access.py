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
