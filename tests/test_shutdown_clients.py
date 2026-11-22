"""A server stopped with browsers still connected exits cleanly.

~webServer deletes the WebSockets that are still open; each one's
destructor closes its socket and emits disconnected, which used to reach
webServer::onWsDisconnected on a server already half torn down (the
FreeDV / RADE / Direwolf threads gone and their processors deleted): three
--shared servers died with SIGSEGV on the way out (2026-11-18 and
2026-11-22, inside ~QWebSocket -> QSslSocket::close -> stateChanged).
"""

from __future__ import annotations

import json
import shutil
import signal
import subprocess
import time

import pytest

from conftest import WFWEB_BIN, launch_shared_wfweb, set_cookie, _run_on_loop
from mock_radio import MockIcomRadio

ADMIN_PW = "club-admin-pw-2026"


@pytest.fixture
def mock(event_loop):
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


def _drain(ws, seconds=1.0):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            ws.recv(timeout=0.1)
        except TimeoutError:
            pass


def _wait_connected(s, cookie):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        j = s.get("/api/v1/radio/info", cookie=cookie).json()
        if (j.get("info", j)).get("connected"):
            return
        time.sleep(0.3)
    pytest.fail("rig never connected")


@pytest.mark.parametrize("clients", [1, 3])
def test_stop_with_browsers_connected(mock, event_loop, tmp_path, clients):
    # The repo's rig files, for a box with none installed.
    shutil.copytree(WFWEB_BIN.parent / "rigs", tmp_path / ".local/share/wfweb/wfweb/rigs")
    s = launch_shared_wfweb(mock, event_loop, tmp_path)
    try:
        r = s.post("/api/v1/auth/setup", {"code": s.setup_code(), "username": "clubadmin",
                                           "password": ADMIN_PW, "callsign": "K1FM"})
        assert r.status_code == 200, r.text
        cookie, _ = set_cookie(r)
        _wait_connected(s, cookie)
        socks = [s.ws(cookie=cookie) for _ in range(clients)]
        # The first one holds the seat, as an operator at the station would.
        socks[0].send(json.dumps({"cmd": "seatTake"}))
        for ws in socks:
            _drain(ws, 0.5)
        s.proc.send_signal(signal.SIGTERM)
        try:
            rc = s.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            pytest.fail("wfweb did not exit within 15 s of SIGTERM")
        assert rc == 0, f"wfweb exited with {rc} (SIGSEGV is -11) on SIGTERM with {clients} browser(s) connected"
        for ws in socks:
            try:
                ws.close()
            except Exception:
                pass
    finally:
        if s.proc.poll() is None:
            s.proc.kill()
            s.proc.wait(timeout=5)
