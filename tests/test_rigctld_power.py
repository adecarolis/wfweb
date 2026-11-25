"""rigctld set_powerstat switches the rig off and on the way the page's power
button does (servermain::powerRigOff / powerRigOn): the queue is cleared and
polling stops on the way down; on the way up the wake-up preamble goes out
(USB) and polling starts again.

A plain 18 01 set queued like any other command never woke a rig that was
off: the queue had been cleared, a CI-V-silent rig cannot answer, and
get_powerstat went back to 0 a moment after the set.

Each test has its own mock and server: switching the rig off would upset the
session-wide instances the other tests share. The mocks are CI-V-silent while
off, like a real rig (CivRadioBase.powered).
"""

import os
import socket
import subprocess
import time

import pytest

from conftest import (WFWEB_BIN, MockIcomRadio, SerialMockRadio, _find_free_port, _launch_wfweb,
                      _run_on_loop, _stop_wfweb, _wait_for_cache, _wait_for_connected)

IC7300 = 0x94


def _rigctl(port, command, timeout=5.0):
    """One command on a fresh connection; the reply lines. An extended (+)
    command ends with RPRT; a plain one answers one line (a value or RPRT)."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        reader = s.makefile("r", encoding="ascii", errors="replace")
        s.sendall((command + "\n").encode())
        lines = []
        while True:
            line = reader.readline()
            if not line:
                break
            lines.append(line.rstrip("\n"))
            if line.startswith("RPRT") or not command.startswith("+"):
                break
        return lines


def _wait(predicate, timeout, step=0.25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(step)
    return predicate()


def _power_frames(mock, mark):
    """Payloads of the 18 xx frames the mock got since civ_log[mark]."""
    out = []
    for f in mock.civ_log[mark:]:
        body = bytes(f).lstrip(b"\xfe")
        if len(body) >= 5 and body[2] == 0x18:
            out.append(body[3:-1])
    return out


def _frames_since(mock, mark):
    return len(mock.civ_log) - mark


@pytest.fixture(scope="module")
def lan_rig(event_loop):
    """An IC-7300 mock over LAN and a server of its own with rigctld."""
    mock = MockIcomRadio(radio_addr=IC7300)
    _run_on_loop(event_loop, mock.start())
    port = _find_free_port()
    proc, _ = _launch_wfweb(mock, event_loop, port, hex(IC7300), "/tmp/wfweb-test-power-lan")
    yield mock, port
    _stop_wfweb(proc)
    _run_on_loop(event_loop, mock.stop())


@pytest.fixture(scope="module")
def usb_rig():
    """An IC-7300 mock on a PTY and a server of its own with rigctld: the USB
    path, where power-on is preceded by the wake-up preamble."""
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")
    mock = SerialMockRadio(radio_addr=IC7300)
    mock.start()
    web_port = _find_free_port()
    port = _find_free_port()
    home = "/tmp/wfweb-test-power-usb"
    os.makedirs(home, exist_ok=True)
    env = os.environ.copy()
    env["HOME"] = home
    proc = subprocess.Popen(
        [str(WFWEB_BIN), "-p", str(web_port), "--serial-port", mock.port_path, "--civ", hex(IC7300),
         "--rigctld-port", str(port), "-l", os.path.join(home, "wfweb.log")],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, env=env)
    rest = f"http://127.0.0.1:{web_port + 1}/api/v1/radio"
    if not (_wait_for_connected(f"{rest}/info") and _wait_for_cache(f"{rest}/status")):
        _stop_wfweb(proc)
        mock.stop()
        pytest.fail("wfweb (USB) did not connect to the mock")
    yield mock, port
    _stop_wfweb(proc)
    mock.stop()


def _off_and_on(mock, port):
    # --- off ---
    mark = len(mock.civ_log)
    assert _rigctl(port, "\\set_powerstat 0") == ["RPRT 0"]
    assert _wait(lambda: not mock.powered, 5), "the rig got no 18 00"
    assert _power_frames(mock, mark)[-1:] == [b"\x00"]
    assert _wait(lambda: _rigctl(port, "\\get_powerstat") == ["0"], 5), _rigctl(port, "\\get_powerstat")

    # Polling stops: what is left is the slow power probe, one read every
    # few seconds, not the status polling of a rig that is on.
    time.sleep(1.0)
    mark_off = len(mock.civ_log)
    time.sleep(6.0)
    assert _frames_since(mock, mark_off) <= 4, f"{_frames_since(mock, mark_off)} frames in 6 s with the rig off"
    assert _rigctl(port, "\\get_powerstat") == ["0"], "the rig is off and silent"

    # --- on ---
    mark = len(mock.civ_log)
    assert _rigctl(port, "\\set_powerstat 1") == ["RPRT 0"]
    assert _wait(lambda: mock.powered, 5), "the rig got no 18 01"
    assert _power_frames(mock, mark)[:1] == [b"\x01"]
    assert _wait(lambda: _rigctl(port, "\\get_powerstat") == ["1"], 10), _rigctl(port, "\\get_powerstat")

    # Polling is back: the server reads the rig again, and get_freq follows
    # the dial.
    mark_on = len(mock.civ_log)
    time.sleep(3.0)
    assert _frames_since(mock, mark_on) > 10, f"only {_frames_since(mock, mark_on)} frames in 3 s after power-on"
    mock.set_frequency(7_074_000)
    assert _wait(lambda: _rigctl(port, "f") == ["7074000"], 10), _rigctl(port, "f")


def test_set_powerstat_switches_a_lan_rig_off_and_on(lan_rig):
    _off_and_on(*lan_rig)


def test_set_powerstat_switches_a_usb_rig_off_and_on(usb_rig):
    mock, port = usb_rig
    mark = len(mock.civ_log)
    _off_and_on(mock, port)
    # On USB the 18 01 goes out behind the wake-up preamble: a run of FE
    # longer than the two of a plain frame.
    wake = [f for f in mock.civ_log[mark:] if bytes(f).lstrip(b"\xfe")[2:4] == b"\x18\x01"]
    assert wake and len(wake[0]) - len(bytes(wake[0]).lstrip(b"\xfe")) > 2, "18 01 without the wake-up preamble"


@pytest.mark.parametrize("value", ["2", "4"])
def test_set_powerstat_standby_and_operate_switch_the_rig_off(lan_rig, value):
    """Hamlib's powerstat_t: only 1 is ON; an Icom goes off for STANDBY (2)
    and OPERATE (4) too (icom_set_powerstat)."""
    mock, port = lan_rig
    assert _rigctl(port, f"\\set_powerstat {value}") == ["RPRT 0"]
    assert _wait(lambda: not mock.powered, 5), f"set_powerstat {value} did not switch the rig off"
    assert _rigctl(port, "\\set_powerstat 1") == ["RPRT 0"]
    assert _wait(lambda: mock.powered, 5)
    assert _wait(lambda: _rigctl(port, "\\get_powerstat") == ["1"], 10)


def test_set_powerstat_1_on_a_rig_that_is_on_keeps_polling(lan_rig):
    """Programs send set_powerstat 1 as they connect. On a rig that is on it
    must not drop the server into the slow power probe."""
    mock, port = lan_rig
    assert _wait(lambda: _rigctl(port, "\\get_powerstat") == ["1"], 10)
    mark = len(mock.civ_log)
    assert _rigctl(port, "+\\set_powerstat 1")[-1] == "RPRT 0"
    assert _wait(lambda: _power_frames(mock, mark) == [b"\x01"], 5), _power_frames(mock, mark)
    time.sleep(1.0)
    mark_on = len(mock.civ_log)
    time.sleep(3.0)
    assert _frames_since(mock, mark_on) > 10, f"only {_frames_since(mock, mark_on)} frames in 3 s: polling dropped"
    mock.set_frequency(3_700_000)
    assert _wait(lambda: _rigctl(port, "f") == ["3700000"], 10), _rigctl(port, "f")
    assert _rigctl(port, "\\get_powerstat") == ["1"]


def _free_udp_ports(count):
    socks = []
    try:
        for _ in range(count):
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.bind(("127.0.0.1", 0))
            socks.append(s)
        return [s.getsockname()[1] for s in socks]
    finally:
        for s in socks:
            s.close()


def test_set_powerstat_through_a_rig_server(tmp_path):
    """A client wfweb (--lan) in front of a wfweb rig server (--no-web) that
    has the rig on USB: the client's rigctld switches the rig behind the
    server off and on. The rig server itself has no rigctld (it lives with
    the web server)."""
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")
    mock = SerialMockRadio(radio_addr=IC7300)
    mock.start()
    control, civ, audio = _free_udp_ports(3)
    server_home = tmp_path / "server"
    server_home.mkdir()
    settings = server_home / "wfweb.conf"
    settings.write_text(
        "[General]\nAudioSystem=0\nManufacturer=0\n\n"
        "[Radios]\n1\\AudioInput=default\n1\\AudioOutput=default\n"
        "1\\RigCIVuInt=0\n1\\RigName=<NONE>\n1\\SerialPortBaud=115200\n"
        "1\\SerialPortRadio=auto\nsize=1\n\n"
        f"[Server]\nServerEnabled=true\nServerControlPort={control}\n"
        f"ServerCivPort={civ}\nServerAudioPort={audio}\n"
        "Users\\size=1\nUsers\\1\\Username=testuser\nUsers\\1\\Password=testpass\nUsers\\1\\UserType=0\n")
    env = dict(os.environ, HOME=str(server_home), XDG_CONFIG_HOME=str(server_home / "config"),
               XDG_DATA_HOME=str(server_home / "data"), WFWEB_NO_TUI="1")
    server = subprocess.Popen(
        [str(WFWEB_BIN), "-s", str(settings), "-l", str(server_home / "wfweb.log"),
         "--serial-port", mock.port_path, "--civ", hex(IC7300), "--no-rigctld", "--no-web"],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, env=env)
    client = None
    try:
        assert _wait(lambda: len(mock.civ_log) > 20, 30), "the rig server did not start polling the mock"
        client_home = tmp_path / "client"
        client_home.mkdir()
        web_port, port = _find_free_port(), _find_free_port()
        client = subprocess.Popen(
            [str(WFWEB_BIN), "-p", str(web_port), "--lan", "127.0.0.1", "--lan-control", str(control),
             "--lan-serial", str(civ), "--lan-audio", str(audio), "--lan-user", "testuser",
             "--lan-pass", "testpass", "--civ", hex(IC7300), "--rigctld-port", str(port),
             "-l", str(client_home / "wfweb.log")],
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
            env=dict(os.environ, HOME=str(client_home), WFWEB_NO_TUI="1"))
        rest = f"http://127.0.0.1:{web_port + 1}/api/v1/radio"
        assert _wait_for_connected(f"{rest}/info", timeout=30) and _wait_for_cache(f"{rest}/status"), \
            "the client did not connect through the rig server"

        assert _rigctl(port, "\\set_powerstat 0") == ["RPRT 0"]
        assert _wait(lambda: not mock.powered, 5), "the rig behind the server got no 18 00"
        assert _wait(lambda: _rigctl(port, "\\get_powerstat") == ["0"], 5)
        time.sleep(3)
        assert _rigctl(port, "\\set_powerstat 1") == ["RPRT 0"]
        assert _wait(lambda: mock.powered, 5), "the rig behind the server got no 18 01"
        assert _wait(lambda: _rigctl(port, "\\get_powerstat") == ["1"], 10)
        mock.set_frequency(10_136_000)
        assert _wait(lambda: _rigctl(port, "f") == ["10136000"], 15), _rigctl(port, "f")
    finally:
        for p in (client, server):
            if p is not None:
                _stop_wfweb(p)
        mock.stop()
