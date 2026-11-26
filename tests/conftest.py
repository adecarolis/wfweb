"""Shared pytest fixtures for wfweb integration tests.

Provides:
  - mock_radio: async MockIcomRadio server (IC-7610 emulation)
  - wfweb: running wfweb subprocess connected to mock_radio
  - rest_url: base URL for the REST API
"""

from __future__ import annotations

import asyncio
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import pytest
import requests

from mock_radio import MockIcomRadio
from mock_serial_radio import SerialMockRadio

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

PROJECT_ROOT = Path(__file__).resolve().parent.parent
WFWEB_BIN = PROJECT_ROOT / "wfweb"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _find_free_port() -> int:
    """Find a free TCP port for the web server, with the next one free too:
    the plain REST port is web port + 1, and with that one taken the server
    runs without it ("Connection refused" in one test run out of seven)."""
    import socket
    for _ in range(50):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as n:
                    n.bind(("127.0.0.1", port + 1))
                return port
            except OSError:
                continue
    raise RuntimeError("no two free TCP ports in a row")


def _wait_for_rest(url: str, timeout: float = 15.0) -> bool:
    """Poll the REST API until it responds or timeout."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            r = requests.get(url, timeout=2)
            if r.status_code == 200:
                return True
        except requests.ConnectionError:
            pass
        time.sleep(0.3)
    return False


def _wait_for_connected(url: str, timeout: float = 15.0) -> bool:
    """Poll until wfweb reports the rig as connected."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            r = requests.get(url, timeout=2)
            if r.status_code == 200:
                data = r.json()
                info = data.get("info", data)
                if info.get("connected", False):
                    return True
        except (requests.ConnectionError, requests.JSONDecodeError):
            pass
        time.sleep(0.3)
    return False


def wfweb_output_tail(proc, lines: int = 60) -> str:
    """The last lines wfweb wrote, for a failure message."""
    path = getattr(proc, "output_path", None)
    if not path or not os.path.exists(path):
        return "(no wfweb output file)"
    with open(path, errors="replace") as f:
        return "".join(f.readlines()[-lines:])


def wfweb_diagnosis(proc, port=None) -> str:
    """What a server that is not answering is doing, for a failure message:
    whether it is still running, how it set up TLS, who listens on its port
    and, on macOS, the stack of every thread (`sample`). The stalls seen on
    the macOS runner only (a WSS upgrade unanswered for 10 s, a start-up that
    never polls) were not reproducible anywhere else, and a log tail does not
    say where a thread is stuck."""
    out = []
    rc = proc.poll()
    out.append(f"wfweb pid {proc.pid}: " + ("running" if rc is None else f"EXITED, return code {rc}"))
    path = getattr(proc, "output_path", None)
    if path and os.path.exists(path):
        with open(path, errors="replace") as f:
            setup = [l.rstrip() for l in f if re.search(r"TLS proxy|SSL|HTTPS|listening on|Internal HTTP", l)]
        out.append("--- TLS / listen lines of the log ---")
        out += setup[:12] + setup[-8:] if len(setup) > 20 else setup

    def run(cmd, limit):
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=40)
            lines = (r.stdout + r.stderr).splitlines()
        except Exception as e:  # a missing tool must not hide the failure itself
            lines = [f"{cmd[0]}: {e}"]
        out.append("--- " + " ".join(cmd) + " ---")
        out.extend(lines[:limit])

    if rc is None and sys.platform == "darwin":
        if port:
            run(["lsof", "-nP", f"-iTCP:{port}"], 20)
        run(["sample", str(proc.pid), "2", "-mayDie"], 450)
    return "\n".join(out)


def ws_tls_context():
    """TLS settings for every WebSocket a test opens with the `websockets`
    sync client: no certificate check (the server's is self-signed) and
    **TLS 1.2 at most**.

    That client reads on a thread of its own while the caller writes. Under
    TLS 1.3 the server's session tickets arrive right after the handshake and
    OpenSSL handles them inside the reader's SSL_read(), at the moment the
    caller's SSL_write() sends the upgrade request: two threads inside one SSL
    object, which OpenSSL does not allow. The request is lost, or the
    connection dies, and the test fails with "WSS upgrade unanswered for
    10 s" or "did not receive a valid HTTP response" while the server never
    saw a request. It took down about one test per run on the macOS runner
    (the only one with the package) and was chased in the server's TLS proxy
    first. Measured 2026-11-16 on Linux: 36 of 2000 connects to a local wfweb
    failed with TLS 1.3 and 0 of 2000 capped at 1.2; a bare Python TLS client
    with a reader thread against a bare Python server lost 1.3 % of 12 000
    requests, and none with TLS 1.2 or with the server's tickets switched off.

    TLS 1.2 has no messages after the handshake, so there is nothing for the
    reader to trip over. Browsers do not work this way, and the REST tests
    (`requests`, one thread) still go through TLS 1.3. **Use this for any new
    WebSocket in a test**; a context of your own brings the flake back."""
    import ssl
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    return ctx


def ws_connect(web_port, proc=None, timeout=10.0):
    """A browser-style WebSocket to a test server: WSS on the web port (TLS
    is on by default), plain WS on the next port only where the server runs
    without TLS. An upgrade the server leaves unanswered fails here with the
    server's log tail: it used to fall through to the plain port, whose HTTP
    200 signpost page said nothing about what the WSS path did (macOS CI,
    2026-10-27; that stall was the client's own, see ws_tls_context). Needs
    the websockets package; skips without it."""
    import ssl
    pytest.importorskip("websockets")
    from websockets.sync.client import connect

    ctx = ws_tls_context()
    try:
        return connect(f"wss://127.0.0.1:{web_port}", ssl=ctx, open_timeout=timeout)
    except TimeoutError as e:
        tail = wfweb_output_tail(proc) if proc is not None else "(no server handle)"
        diag = wfweb_diagnosis(proc, web_port) if proc is not None else ""
        pytest.fail(f"WSS upgrade on port {web_port} unanswered for {timeout} s: {e}\n"
                    f"--- wfweb output (tail) ---\n{tail}\n{diag}")
    except (ssl.SSLError, OSError):
        return connect(f"ws://127.0.0.1:{web_port + 1}", open_timeout=timeout)


def _dump_and_fail(proc, event_loop, loop_thread, message, url):
    """Kill wfweb, dump its output, stop the event loop, and fail the test."""
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
    print(f"=== wfweb output ({message}) ===", file=sys.stderr)
    print(wfweb_output_tail(proc, 400), file=sys.stderr)
    event_loop.call_soon_threadsafe(event_loop.stop)
    loop_thread.join(timeout=2)
    pytest.fail(f"{message}. URL: {url}")


def _wait_for_cache(status_url: str, timeout: float = 30.0) -> bool:
    """Poll /status until frequency AND mode caches are both populated.

    Mode lands in a later poll cycle than frequency, so waiting only on
    frequency races test_status_has_core_fields.  Timeout is generous
    because slow runners (macOS, Apple Silicon) sometimes take >10 s for
    the first full poll cycle to land.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            r = requests.get(status_url, timeout=2)
            if r.status_code == 200:
                data = r.json()
                # frequency/mode are null (not absent) until the first reply
                if ((data.get("frequency") or 0) > 0
                        and isinstance(data.get("mode"), str)
                        and data["mode"]):
                    return True
        except (requests.ConnectionError, requests.JSONDecodeError):
            pass
        time.sleep(0.3)
    return False


# ---------------------------------------------------------------------------
# Mock radio fixture
# ---------------------------------------------------------------------------


_loop_thread = None


def _ensure_loop_thread(event_loop):
    """Run the shared asyncio loop in a daemon thread, started once per session.

    The mock radios need the loop pumped while wfweb handshakes; the thread
    outlives every wfweb instance and is stopped by the event_loop fixture.
    """
    global _loop_thread
    if _loop_thread is None:
        import threading
        started = threading.Event()

        def run_loop():
            asyncio.set_event_loop(event_loop)
            started.set()
            event_loop.run_forever()

        _loop_thread = threading.Thread(target=run_loop, daemon=True)
        _loop_thread.start()
        started.wait()
    return _loop_thread


def _run_on_loop(event_loop, coro):
    """Run a coroutine on the shared loop, whether or not its thread is up yet."""
    if event_loop.is_running():
        return asyncio.run_coroutine_threadsafe(coro, event_loop).result(timeout=10)
    return event_loop.run_until_complete(coro)


@pytest.fixture(scope="session")
def event_loop():
    """Session-scoped event loop for the mock radios."""
    global _loop_thread
    loop = asyncio.new_event_loop()
    yield loop
    if _loop_thread is not None:
        loop.call_soon_threadsafe(loop.stop)
        _loop_thread.join(timeout=2)
        _loop_thread = None
    loop.close()


@pytest.fixture(scope="session")
def mock_radio(event_loop):
    """Start a MockIcomRadio (IC-7610) for the entire test session."""
    server = MockIcomRadio()
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


@pytest.fixture(scope="session")
def mock_radio_7300(event_loop):
    """A second mock answering as an IC-7300 (CI-V 0x94): no command 29, so
    the server addresses the operating VFO through the 25 00 / 26 00 slots."""
    server = MockIcomRadio(radio_addr=0x94)
    _run_on_loop(event_loop, server.start())
    yield server
    _run_on_loop(event_loop, server.stop())


# ---------------------------------------------------------------------------
# wfweb subprocess fixture
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def rigctld_port():
    """TCP port of the rigctld server started with wfweb_instance (localhost only)."""
    return _find_free_port()


@pytest.fixture(scope="session")
def rigctld_7300_port():
    """TCP port of the rigctld server started with wfweb_7300_instance."""
    return _find_free_port()


def _launch_wfweb(mock, event_loop, rigctld_port, civ, home):
    """Start wfweb LAN-connected to mock; return (process, web_port) once the
    rig is connected and its frequency / mode caches are live."""
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN} — run 'qmake wfweb.pro && make' first")

    web_port = _find_free_port()
    rest_port = web_port + 1

    cmd = [
        str(WFWEB_BIN),
        "-p", str(web_port),
        "--lan", "127.0.0.1",
        "--lan-control", str(mock.control_port),
        "--lan-user", "testuser",
        "--lan-pass", "testpass",
        "--civ", civ,
        "--rigctld-port", str(rigctld_port),
    ]

    env = os.environ.copy()
    # Avoid wfweb loading the user's real settings
    env["HOME"] = home
    os.makedirs(home, exist_ok=True)

    # wfweb also logs every line to stdout when it is not on a terminal. A pipe
    # nobody reads fills up (16 KB on macOS) and then blocks every thread that
    # logs, so stdout is dropped; the -l log file, flushed per line, is what
    # failures quote.
    output_path = os.path.join(home, "wfweb.log")
    cmd += ["-l", output_path]
    capture = os.environ.get("WFWEB_TEST_CAPTURE")   # see launch_shared_wfweb
    proc = subprocess.Popen(
        cmd,
        stdout=open(os.path.join(home, "wfweb.stderr"), "wb") if capture else subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
        env=env,
    )
    proc.output_path = output_path

    # The mock's event loop must run in the background to handle the handshake
    loop_thread = _ensure_loop_thread(event_loop)

    rest_base = f"http://127.0.0.1:{rest_port}"
    info_url = f"{rest_base}/api/v1/radio/info"

    if not _wait_for_connected(info_url, timeout=15.0):
        _dump_and_fail(proc, event_loop, loop_thread,
                       "wfweb did not connect within timeout", info_url)

    # Wait for the first poll cycle to populate frequency AND mode caches
    status_url = f"{rest_base}/api/v1/radio/status"
    if not _wait_for_cache(status_url):
        _dump_and_fail(proc, event_loop, loop_thread,
                       "wfweb cache not populated within timeout", status_url)

    return proc, web_port


def _stop_wfweb(proc):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


@pytest.fixture(scope="session")
def wfweb_instance(mock_radio, event_loop, rigctld_port):
    """Start wfweb connected to mock_radio, yield (process, web_port), stop on teardown."""
    proc, web_port = _launch_wfweb(mock_radio, event_loop, rigctld_port, "0x98", "/tmp/wfweb-test")
    yield proc, web_port
    _stop_wfweb(proc)


@pytest.fixture(scope="session")
def wfweb_7300_instance(mock_radio_7300, event_loop, rigctld_7300_port):
    """wfweb connected to the IC-7300 mock: for the rigctld tests that need the
    Selected / Unselected VFO slots and a mode set that carries the data byte."""
    proc, web_port = _launch_wfweb(mock_radio_7300, event_loop, rigctld_7300_port, "0x94", "/tmp/wfweb-test-7300")
    yield proc, web_port
    _stop_wfweb(proc)


@pytest.fixture(scope="session")
def rest_url(wfweb_instance):
    """Base REST API URL: http://127.0.0.1:<port>/api/v1/radio"""
    _, web_port = wfweb_instance
    return f"http://127.0.0.1:{web_port + 1}/api/v1/radio"


@pytest.fixture(scope="session")
def poll(rest_url):
    """Return a function that polls a REST endpoint until a condition is met.

    Usage::
        data = poll("frequency", lambda d: d.get("hz") == 7074000)
    """
    def _poll(endpoint, predicate, timeout=5.0):
        url = f"{rest_url}/{endpoint}"
        deadline = time.monotonic() + timeout
        last_data = None
        while time.monotonic() < deadline:
            try:
                r = requests.get(url, timeout=2)
                if r.status_code == 200:
                    last_data = r.json() if r.text else {}
                    if predicate(last_data):
                        return last_data
            except (requests.ConnectionError, requests.JSONDecodeError):
                pass
            time.sleep(0.2)
        pytest.fail(f"poll({endpoint}) timed out. Last: {last_data}")
    return _poll


# ---------------------------------------------------------------------------
# USB/serial mock fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def serial_mock():
    """Start a SerialMockRadio (PTY-based) for the entire test session."""
    server = SerialMockRadio()
    server.start()
    yield server
    server.stop()


@pytest.fixture(scope="session")
def usb_wfweb_instance(serial_mock):
    """Start wfweb connected to serial_mock via PTY, yield (process, web_port)."""
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")

    web_port = _find_free_port()
    rest_port = web_port + 1

    cmd = [
        str(WFWEB_BIN),
        "-p", str(web_port),
        "--serial-port", serial_mock.port_path,
        "--civ", "0x98",
    ]

    env = os.environ.copy()
    env["HOME"] = "/tmp/wfweb-test-usb"
    os.makedirs("/tmp/wfweb-test-usb", exist_ok=True)

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )

    rest_base = f"http://127.0.0.1:{rest_port}"
    info_url = f"{rest_base}/api/v1/radio/info"

    if not _wait_for_connected(info_url, timeout=15.0):
        proc.terminate()
        try:
            out, _ = proc.communicate(timeout=3)
            print(f"=== wfweb USB output ===", file=sys.stderr)
            print(out.decode(errors="replace"), file=sys.stderr)
        except subprocess.TimeoutExpired:
            proc.kill()
        pytest.fail(f"wfweb (USB) did not connect. PTY: {serial_mock.port_path}")

    status_url = f"{rest_base}/api/v1/radio/status"
    if not _wait_for_cache(status_url):
        proc.terminate()
        try:
            out, _ = proc.communicate(timeout=3)
            print(f"=== wfweb USB output (cache) ===", file=sys.stderr)
            print(out.decode(errors="replace"), file=sys.stderr)
        except subprocess.TimeoutExpired:
            proc.kill()
        pytest.fail("wfweb (USB) cache not populated")

    yield proc, web_port

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


@pytest.fixture(scope="session")
def usb_rest_url(usb_wfweb_instance):
    """Base REST API URL for the USB-connected wfweb instance."""
    _, web_port = usb_wfweb_instance
    return f"http://127.0.0.1:{web_port + 1}/api/v1/radio"


@pytest.fixture(scope="session")
def usb_poll(usb_rest_url):
    """Poll fixture for the USB-connected wfweb instance."""
    def _poll(endpoint, predicate, timeout=5.0):
        url = f"{usb_rest_url}/{endpoint}"
        deadline = time.monotonic() + timeout
        last_data = None
        while time.monotonic() < deadline:
            try:
                r = requests.get(url, timeout=2)
                if r.status_code == 200:
                    last_data = r.json() if r.text else {}
                    if predicate(last_data):
                        return last_data
            except (requests.ConnectionError, requests.JSONDecodeError):
                pass
            time.sleep(0.2)
        pytest.fail(f"usb_poll({endpoint}) timed out. Last: {last_data}")
    return _poll


# ---------------------------------------------------------------------------
# Shared Station (--shared) server, for test_access.py and test_access_roles.py
# ---------------------------------------------------------------------------

SETUP_CODE_RE = re.compile(r"setup code ([A-Z2-9]{4}-[A-Z2-9]{4})")
SESSION_COOKIE = "wfweb_session"


class SharedServer:
    """A --shared wfweb: URLs, the setup code from its log, request helpers."""

    def __init__(self, proc, web_port, shared_dir, log_path):
        self.proc = proc
        self.web_port = web_port
        self.https = f"https://127.0.0.1:{web_port}"
        self.http = f"http://127.0.0.1:{web_port + 1}"
        self.shared_dir = shared_dir
        self.log_path = log_path
        self.admin_cookie = None       # set once the admin exists, used by the rest
        self.admin_password = "club-admin-pw-2026"

    def setup_code(self):
        with open(self.log_path, errors="replace") as f:
            codes = SETUP_CODE_RE.findall(f.read())
        assert codes, f"no setup code in the log:\n{wfweb_output_tail(self.proc)}"
        return codes[-1]

    def req(self, method, path, *, cookie=None, token=None, plain=False, **kw):
        base = self.http if plain else self.https
        headers = dict(kw.pop("headers", {}) or {})
        if token:
            headers["Authorization"] = f"Bearer {token}"
        cookies = {SESSION_COOKIE: cookie} if cookie else None
        return requests.request(method, base + path, headers=headers, cookies=cookies,
                                verify=False, timeout=10, **kw)

    def get(self, path, **kw):
        return self.req("GET", path, **kw)

    def post(self, path, body=None, **kw):
        return self.req("POST", path, json=body or {}, **kw)

    def ws(self, cookie=None, timeout=10.0):
        """A browser-style WSS socket, with the session cookie when given."""
        pytest.importorskip("websockets")
        import ssl
        from websockets.sync.client import connect
        ctx = ws_tls_context()
        headers = {"Cookie": f"{SESSION_COOKIE}={cookie}"} if cookie else None
        try:
            return connect(f"wss://127.0.0.1:{self.web_port}", ssl=ctx, open_timeout=timeout,
                           additional_headers=headers)
        except TimeoutError as e:
            # The stall that used to land here was the client's own (see
            # ws_tls_context). One that still does is the server's: say what
            # it was doing.
            pytest.fail(f"WSS upgrade on port {self.web_port} unanswered for {timeout} s: {e}\n"
                        f"--- wfweb output (tail) ---\n{wfweb_output_tail(self.proc)}\n"
                        f"{wfweb_diagnosis(self.proc, self.web_port)}")


def set_cookie(resp):
    """The session cookie a response sets, with its attributes."""
    header = resp.headers.get("Set-Cookie", "")
    m = re.match(rf"{SESSION_COOKIE}=([^;]*);(.*)", header)
    assert m, f"no session cookie in {header!r}"
    return m.group(1), m.group(2)


def launch_shared_wfweb(mock, event_loop, home, extra_args=(), output=None):
    """Start wfweb --shared against mock, its accounts under home/station,
    and wait until the auth routes answer (the rig may still be connecting).
    Stop it with _stop_wfweb(server.proc).  output: a path for the
    process's stdout / stderr (sanitizer reports); dropped by default."""
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")
    home = Path(home)
    shared_dir = home / "station"
    web_port = _find_free_port()
    log_path = home / "wfweb.log"
    cmd = [
        str(WFWEB_BIN), "-p", str(web_port),
        "--lan", "127.0.0.1", "--lan-control", str(mock.control_port),
        "--lan-user", "testuser", "--lan-pass", "testpass", "--civ", "0x98",
        "--shared", "--shared-dir", str(shared_dir),
        "-l", str(log_path), *extra_args,
    ]
    # The settings file follows XDG_CONFIG_HOME when it is set (GitHub's
    # runners set it), not HOME: without this every --shared test server
    # shared one file and one module's policy was the next one's start.
    env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"))
    # WFWEB_TEST_CAPTURE=1 keeps every test server's stdout / stderr (a
    # sanitizer report, a qFatal) in <home>/wfweb.stderr for a post-mortem.
    if output is None and os.environ.get("WFWEB_TEST_CAPTURE"):
        output = home / "wfweb.stderr"
    out = open(output, "wb") if output else subprocess.DEVNULL
    proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env)
    proc.output_path = str(log_path)
    _ensure_loop_thread(event_loop)
    srv = SharedServer(proc, web_port, shared_dir, str(log_path))

    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            if srv.get("/api/v1/auth/me").status_code == 200:
                return srv
        except requests.ConnectionError:
            pass
        if proc.poll() is not None:
            pytest.fail(f"wfweb exited with {proc.returncode}:\n{wfweb_output_tail(proc)}")
        time.sleep(0.3)
    _stop_wfweb(proc)
    pytest.fail(f"--shared wfweb not answering:\n{wfweb_output_tail(proc)}")
