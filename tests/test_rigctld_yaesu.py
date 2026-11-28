"""A Yaesu rig on USB, through the Yaesu commander: identification and the
rigctld clock (DT0 / DT1).

- An FT-710 answers ID with 0800 and an FTX-1 with 0840, above 255: the rig
  list was built with a one-byte key, so neither was ever found and the
  server never loaded a rig file for them.
- get_clock answers from the cached System Date / Time. The Yaesu commander
  only logged the DT0 / DT1 replies, so get_clock answered RPRT -5 every
  time on these rigs.

The mock (tests/mock_yaesu_radio.py) answers as an FT-710 on a PTY.
"""

import os
import socket
import subprocess
import time

import pytest
import requests

from conftest import WFWEB_BIN, _find_free_port, _stop_wfweb
from mock_yaesu_radio import YaesuMockRadio


def _rigctl(port, command, timeout=5.0):
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


def _wait(predicate, timeout, step=0.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(step)
    return predicate()


@pytest.fixture(scope="module")
def ft710(tmp_path_factory):
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")
    mock = YaesuMockRadio()
    mock.start()
    home = tmp_path_factory.mktemp("yaesu")
    web_port, port = _find_free_port(), _find_free_port()
    proc = subprocess.Popen(
        [str(WFWEB_BIN), "-p", str(web_port), "--serial-port", mock.port_path, "--manufacturer", "2",
         "--rigctld-port", str(port), "-l", str(home / "wfweb.log")],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
        env=dict(os.environ, HOME=str(home), WFWEB_NO_TUI="1"))
    proc.output_path = str(home / "wfweb.log")
    info_url = f"http://127.0.0.1:{web_port + 1}/api/v1/radio/info"

    def info():
        try:
            r = requests.get(info_url, timeout=2)
            return r.json() if r.status_code == 200 else None
        except (requests.ConnectionError, ValueError):
            return None

    yield mock, port, info
    _stop_wfweb(proc)
    mock.stop()


def test_an_ft710_is_identified_by_its_id(ft710):
    mock, _, info = ft710
    got = _wait(lambda: (i := info()) and i.get("connected") and i, 30)
    assert got, f"the FT-710 mock was never identified (CAT seen: {sorted(set(c[:3] for c in mock.cat_log))})"
    assert "ID" in mock.cat_log
    assert got.get("model") == "FT-710", got.get("model")


def test_get_clock_reads_the_rigs_date_and_time(ft710):
    mock, port, info = ft710
    assert _wait(lambda: (i := info()) and i.get("connected"), 30)
    mock.date, mock.time = "20240102", "030405"
    assert _wait(lambda: _rigctl(port, "\\get_clock") == ["2024-01-02T03:04:00.000+00:00"], 30), \
        _rigctl(port, "\\get_clock")
    # Read from the rig, not from what the server wrote: a change on the
    # radio itself comes through when the cached reading is asked again.
    mock.date, mock.time = "20250304", "050607"
    assert _wait(lambda: _rigctl(port, "\\get_clock") == ["2025-03-04T05:06:00.000+00:00"], 60), \
        _rigctl(port, "\\get_clock")
    assert any(c == "DT0" for c in mock.cat_log) and any(c == "DT1" for c in mock.cat_log)


def test_set_clock_writes_dt0_dt1_and_get_clock_reads_them_back(ft710):
    mock, port, info = ft710
    assert _wait(lambda: (i := info()) and i.get("connected"), 30)
    assert _rigctl(port, "\\set_clock 2030-06-07T09:41:30+00") == ["RPRT 0"]
    assert _wait(lambda: mock.date == "20300607" and mock.time == "094100", 10), (mock.date, mock.time)
    assert "DT020300607" in mock.cat_log and "DT1094100" in mock.cat_log
    assert _wait(lambda: _rigctl(port, "\\get_clock") == ["2030-06-07T09:41:00.000+00:00"], 30), \
        _rigctl(port, "\\get_clock")
