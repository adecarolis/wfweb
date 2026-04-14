"""Shared pytest fixtures for wfweb integration tests.

Provides:
  - mock_radio: async MockIcomRadio server (IC-7610 emulation)
  - wfweb: running wfweb subprocess connected to mock_radio
  - rest_url: base URL for the REST API
"""

from __future__ import annotations

import asyncio
import os
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
    """Find a free TCP port for the web server."""
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


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


def _dump_and_fail(proc, event_loop, loop_thread, message, url):
    """Kill wfweb, dump its output, stop the event loop, and fail the test."""
    proc.terminate()
    try:
        out, _ = proc.communicate(timeout=3)
        print(f"=== wfweb output ({message}) ===", file=sys.stderr)
        print(out.decode(errors="replace"), file=sys.stderr)
    except subprocess.TimeoutExpired:
        proc.kill()
    event_loop.call_soon_threadsafe(event_loop.stop)
    loop_thread.join(timeout=2)
    pytest.fail(f"{message}. URL: {url}")


def _wait_for_cache(freq_url: str, timeout: float = 10.0) -> bool:
    """Poll until the frequency cache is populated (first poll cycle complete)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            r = requests.get(freq_url, timeout=2)
            if r.status_code == 200:
                data = r.json()
                if "hz" in data and data["hz"] > 0:
                    return True
        except (requests.ConnectionError, requests.JSONDecodeError):
            pass
        time.sleep(0.3)
    return False


# ---------------------------------------------------------------------------
# Mock radio fixture
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def event_loop():
    """Session-scoped event loop for the mock radio."""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest.fixture(scope="session")
def mock_radio(event_loop):
    """Start a MockIcomRadio for the entire test session."""
    server = MockIcomRadio()
    event_loop.run_until_complete(server.start())
    yield server
    event_loop.run_until_complete(server.stop())


# ---------------------------------------------------------------------------
# wfweb subprocess fixture
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def wfweb_instance(mock_radio, event_loop):
    """Start wfweb connected to mock_radio, yield (process, web_port), stop on teardown."""

    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN} — run 'qmake wfweb.pro && make' first")

    web_port = _find_free_port()
    rest_port = web_port + 1

    cmd = [
        str(WFWEB_BIN),
        "-p", str(web_port),
        "--lan", "127.0.0.1",
        "--lan-control", str(mock_radio.control_port),
        "--lan-user", "testuser",
        "--lan-pass", "testpass",
        "--civ", "0x98",
    ]

    env = os.environ.copy()
    # Avoid wfweb loading the user's real settings
    env["HOME"] = "/tmp/wfweb-test"
    os.makedirs("/tmp/wfweb-test", exist_ok=True)

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )

    # Let the mock's event loop run in the background to handle handshake
    # We need to pump the asyncio loop while waiting for wfweb to connect
    import threading

    loop_running = threading.Event()

    def run_loop():
        asyncio.set_event_loop(event_loop)
        loop_running.set()
        event_loop.run_forever()

    loop_thread = threading.Thread(target=run_loop, daemon=True)
    loop_thread.start()
    loop_running.wait()

    rest_base = f"http://127.0.0.1:{rest_port}"
    info_url = f"{rest_base}/api/v1/radio/info"

    connected = _wait_for_connected(info_url, timeout=15.0)

    if not connected:
        _dump_and_fail(proc, event_loop, loop_thread,
                       "wfweb did not connect within timeout", info_url)

    # Wait for the first poll cycle to populate frequency/mode cache
    freq_url = f"{rest_base}/api/v1/radio/frequency"
    cache_ready = _wait_for_cache(freq_url, timeout=10.0)

    if not cache_ready:
        _dump_and_fail(proc, event_loop, loop_thread,
                       "wfweb cache not populated within timeout", freq_url)

    yield proc, web_port

    # Teardown
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)

    event_loop.call_soon_threadsafe(event_loop.stop)
    loop_thread.join(timeout=2)


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

    freq_url = f"{rest_base}/api/v1/radio/frequency"
    if not _wait_for_cache(freq_url, timeout=10.0):
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
