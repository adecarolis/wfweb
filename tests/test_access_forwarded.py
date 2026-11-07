"""The client address behind a trusted proxy (--trust-proxy).

Cloudflare and nginx do not replace a client-supplied X-Forwarded-For, they
append the connecting address to it, so the first entry is the client's to
choose.  wfweb used to take that first entry: a brute-forcer behind such a
proxy could rotate invented addresses past the per-address lockout and
write them into the audit log.  AccessControl::forwardedClient now takes
the rightmost entry that is not itself a trusted proxy, on the REST path
(requestContext) and on the WebSocket upgrade (onWsNewConnection) alike.

This module runs its own --shared server with --trust-proxy 127.0.0.1, so
every request from the test process arrives "through the proxy" and its
X-Forwarded-For is believed.
"""

import json
import ssl

import pytest

from conftest import SESSION_COOKIE, launch_shared_wfweb, set_cookie, _run_on_loop, _stop_wfweb
from mock_radio import MockIcomRadio

ADMIN_PW = "club-admin-pw-2026"


@pytest.fixture(scope="module")
def fwd_mock(event_loop):
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


@pytest.fixture(scope="module")
def srv(fwd_mock, event_loop, tmp_path_factory):
    home = tmp_path_factory.mktemp("fwd-home")
    s = launch_shared_wfweb(fwd_mock, event_loop, home, extra_args=("--trust-proxy", "127.0.0.1"))
    r = s.post("/api/v1/auth/setup", {"code": s.setup_code(), "username": "clubadmin",
                                       "password": ADMIN_PW, "callsign": "K1FM"})
    assert r.status_code == 200, r.text
    s.admin_cookie, _ = set_cookie(r)
    yield s
    _stop_wfweb(s.proc)


def audit(srv):
    return (srv.shared_dir / "audit.log").read_text()


def miss(srv, name, forwarded_for):
    """One failed login as an unknown name, with the given X-Forwarded-For."""
    return srv.post("/api/v1/auth/login", {"username": name, "password": "wrong-wrong"},
                    headers={"X-Forwarded-For": forwarded_for})


def test_audit_records_the_address_that_reached_the_proxy(srv):
    # The client started the list with 1.2.3.4; the proxy appended the
    # address it saw.  The audit line carries the latter.
    assert miss(srv, "nobody", "1.2.3.4, 203.0.113.9").status_code == 401
    lines = [l for l in audit(srv).splitlines() if "login-failed nobody@" in l]
    assert lines and "nobody@203.0.113.9" in lines[-1], lines
    assert "1.2.3.4" not in audit(srv)


def test_a_proxys_own_address_is_skipped(srv):
    # An inner hop appended the trusted proxy's address: walk past it.
    assert miss(srv, "noone", "203.0.113.10, 127.0.0.1").status_code == 401
    lines = [l for l in audit(srv).splitlines() if "login-failed noone@" in l]
    assert lines and "noone@203.0.113.10" in lines[-1], lines


def test_lockout_counts_the_real_address_not_the_first_entry(srv):
    # Five misses from 198.51.100.7, each under a new unknown name and a new
    # invented first entry: the per-name lockout never trips, the
    # per-address one must, on the fifth (kLockoutFailures).
    for i in range(4):
        r = miss(srv, f"ghost{i}", f"10.{i}.0.1, 198.51.100.7")
        assert r.status_code == 401, (i, r.text)
    r = miss(srv, "ghost9", "10.99.0.1, 198.51.100.7")
    assert r.status_code == 429, r.text
    # Another client behind the same proxy is not locked out ...
    assert miss(srv, "ghost8", "198.51.100.8").status_code == 401
    # ... and neither is the proxy's own address.
    r = srv.post("/api/v1/auth/login", {"username": "clubadmin", "password": ADMIN_PW})
    assert r.status_code == 200, r.text


def test_websocket_connection_lists_the_forwarded_address(srv):
    pytest.importorskip("websockets")
    from websockets.sync.client import connect
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    headers = {"Cookie": f"{SESSION_COOKIE}={srv.admin_cookie}", "X-Forwarded-For": "1.2.3.4, 203.0.113.9"}
    with connect(f"wss://127.0.0.1:{srv.web_port}", ssl=ctx, open_timeout=10, additional_headers=headers) as ws:
        first = json.loads(ws.recv(timeout=10))
        assert first["type"] == "access" and first["user"]["name"] == "clubadmin"
        conns = srv.get("/api/v1/shared/connections", cookie=srv.admin_cookie).json()["connections"]
        ips = [c["ip"] for c in conns]
        assert "203.0.113.9" in ips and "1.2.3.4" not in ips, ips
