"""A short fuzz pass over the HTTP parser and the WebSocket handlers
(tests/fuzz_webserver.py), on a plain server and on a --shared one.

Each run gets its own wfweb: a server the fuzzer kills must not take the
session's instance with it, and the shared one needs its own account store.
When the binary carries AddressSanitizer / UBSan (a hand build:
    qmake wfweb.pro CONFIG+=debug "QMAKE_CXXFLAGS+=-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -g1 -O1" "QMAKE_CFLAGS+=-fsanitize=address,undefined" "QMAKE_LFLAGS+=-fsanitize=address,undefined"
with ASAN_OPTIONS=detect_leaks=0) their reports land on the process's
stderr, kept here in sanitizer.txt under the server's home; an
AddressSanitizer report, or a UBSan one from the web server's own files,
fails the test like a crash does.  (UBSan prints and carries on, so the
health check alone would miss it; and the start-up reports from the radio
side -- uninitialised enums in rigidentities.h, icomudpbase.h -- are not
this parser's and are left alone.)  On the plain CI binary the files stay
empty and only the crash / hang check applies.

Deterministic and short -- the targeted cases in batches plus 100 random
ones per side, about a minute a side; set WFWEB_FUZZ_SECONDS for a longer
random phase by hand.
"""

import os
import subprocess
import sys
import time
from pathlib import Path

import pytest
import requests

sys.path.insert(0, str(Path(__file__).resolve().parent))
from conftest import (PROJECT_ROOT, WFWEB_BIN, MockIcomRadio, _ensure_loop_thread, _find_free_port,   # noqa: E402
                      _run_on_loop, _stop_wfweb, launch_shared_wfweb, set_cookie, wfweb_output_tail)
import fuzz_webserver  # noqa: E402

RANDOM_ITERATIONS = 100
SECONDS = float(os.environ.get("WFWEB_FUZZ_SECONDS", "0")) or None


PARSER_FILES = ("webserver.cpp", "webserver.h", "access.cpp", "access.h", "logbook.cpp", "memorystore.cpp",
                "wsjtxmessage.cpp")


def _sanitizer_env(home):
    """ASan / UBSan options for a server under test: no leak check at exit
    (Qt's plugin loaders leak by design and the servers are killed anyway),
    no halt on a UBSan report (the radio side has a few at start-up)."""
    env = dict(os.environ, HOME=str(home))
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1"
    return env


def _sanitizer_reports(home):
    """ASan reports, and UBSan reports placed in the web server's files."""
    path = Path(home) / "sanitizer.txt"
    if not path.exists():
        return ""
    with open(path, errors="replace") as f:
        lines = f.read().splitlines()
    out = []
    for i, line in enumerate(lines):
        if "AddressSanitizer" in line and "ERROR" in line:
            out.append("\n".join(lines[i:i + 30]))
        elif "runtime error" in line and any(f in line for f in PARSER_FILES):
            out.append("\n".join(lines[i:i + 12]))
    return "\n---\n".join(out)


def _checker(home):
    def check():
        reports = _sanitizer_reports(home)
        return f"sanitizer report:\n{reports}" if reports else None
    return check


@pytest.fixture(scope="module")
def fuzz_mock(event_loop):
    server = MockIcomRadio(radio_addr=0x98)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


def _launch_plain(mock, event_loop, home):
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")
    web_port = _find_free_port()
    log_path = home / "wfweb.log"
    cmd = [str(WFWEB_BIN), "-p", str(web_port), "--lan", "127.0.0.1", "--lan-control", str(mock.control_port),
           "--lan-user", "testuser", "--lan-pass", "testpass", "--civ", "0x98", "-l", str(log_path)]
    proc = subprocess.Popen(cmd, stdout=open(home / "sanitizer.txt", "wb"), stderr=subprocess.STDOUT,
                            env=_sanitizer_env(home))
    proc.output_path = str(log_path)
    _ensure_loop_thread(event_loop)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            r = requests.get(f"http://127.0.0.1:{web_port + 1}/api/v1/radio/info", timeout=2)
            if r.status_code == 200 and (r.json().get("info", r.json())).get("connected"):
                return proc, web_port
        except (requests.ConnectionError, requests.JSONDecodeError):
            pass
        if proc.poll() is not None:
            pytest.fail(f"wfweb exited with {proc.returncode}:\n{wfweb_output_tail(proc)}")
        time.sleep(0.3)
    _stop_wfweb(proc)
    pytest.fail(f"wfweb not up:\n{wfweb_output_tail(proc)}")


def _run(fz, side, proc, home):
    ok = fz.run(side, RANDOM_ITERATIONS, SECONDS)
    reports = _sanitizer_reports(home)
    detail = "\n".join(f"{f['batch']}: {f['effect']}\n  " + "\n  ".join(
        str(i["input"].get("repr") or i["input"].get("text") or i["input"]) for i in f["inputs"][:25])
        for f in fz.findings)
    assert ok and not reports, (f"{len(fz.findings)} finding(s) after {fz.cases} cases:\n{detail}\n"
                                f"{reports}\n--- wfweb log (tail) ---\n{wfweb_output_tail(proc, 40)}")
    print(f"{side}: {fz.cases} cases in {fz.batches} batches, {fz.elapsed:.0f} s" + "".join(f"; {n}" for n in fz.notes))


@pytest.fixture(scope="module")
def plain_server(fuzz_mock, event_loop, tmp_path_factory):
    home = tmp_path_factory.mktemp("fuzz-plain")
    proc, port = _launch_plain(fuzz_mock, event_loop, home)
    yield proc, port, home
    _stop_wfweb(proc)


@pytest.fixture(scope="module")
def shared_server(fuzz_mock, event_loop, tmp_path_factory):
    """A --shared server with visitors allowed to listen: the configuration
    that puts the parser in front of the internet."""
    home = tmp_path_factory.mktemp("fuzz-shared")
    # launch_shared_wfweb sets HOME itself; the sanitizer options ride on the
    # environment it copies.
    saved = {k: os.environ.get(k) for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS")}
    os.environ.update({k: v for k, v in _sanitizer_env(home).items() if k in saved})
    try:
        srv = launch_shared_wfweb(fuzz_mock, event_loop, home, output=home / "sanitizer.txt")
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    r = srv.post("/api/v1/auth/setup", {"code": srv.setup_code(), "username": "clubadmin",
                                         "password": srv.admin_password, "callsign": "K1FM"})
    assert r.status_code == 200, r.text
    srv.admin_cookie, _ = set_cookie(r)
    r = srv.req("PUT", "/api/v1/shared/policy", json={"anonymous": "listen", "seat": False}, cookie=srv.admin_cookie)
    assert r.status_code == 200, r.text
    yield srv, home
    _stop_wfweb(srv.proc)


@pytest.mark.parametrize("side", ["http", "ws"])
def test_plain_server_survives(plain_server, side):
    proc, port, home = plain_server
    fz = fuzz_webserver.Fuzzer(port, shared=False, seed=1, pid=proc.pid, proc=proc, check=_checker(home), quick=True)
    _run(fz, side, proc, home)


@pytest.mark.parametrize("side", ["http", "ws"])
def test_shared_server_survives_as_visitor(shared_server, side):
    """Everything an anonymous visitor can send: no cookie, no token."""
    srv, home = shared_server
    fz = fuzz_webserver.Fuzzer(srv.web_port, shared=True, seed=2, pid=srv.proc.pid, proc=srv.proc, check=_checker(home), quick=True)
    _run(fz, side, srv.proc, home)


def test_shared_server_refuses_unknown_commands(shared_server):
    """A command the capability table does not know is admin-only: a
    visitor gets 'denied' with need admin, and nothing else happens."""
    srv, home = shared_server
    import json
    with srv.ws() as ws:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            m = json.loads(ws.recv(timeout=5))
            if m.get("type") == "access":
                assert m["role"] == "listen"
                break
        for cmd in ("nosuchcommand", "", "SETFREQUENCY", "seatX"):
            ws.send(json.dumps({"cmd": cmd, "value": 1}))
            while True:
                m = json.loads(ws.recv(timeout=5))
                if m.get("type") == "denied":
                    assert m["cmd"] == cmd and m["need"] == "admin", m
                    break
