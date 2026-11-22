"""AGC: the FUNC AGC tile's server side, on the IC-7300 mock (CI-V 16 12).

The values the AGC command takes differ per rig (IC-7300 1 FAST / 2 MID /
3 SLOW, IC-7600 0-2, IC-7200 and the Kenwoods with an OFF), so the rig file
names them (AGC\\N\\Num, Name) and rigInfo carries the list as agcModes. The
page steps through it; the server sets 16 12, reads it back and pushes the
value as agc. rigctld's AGC level maps hamlib's agc_level_e through the same
names.

Needs the repo's rigs/IC-7300.rig (2.13, with the AGC list) where the server
looks for rig files: CI copies rigs/*.rig to /usr/local/share/wfview/rigs.

tests/agc_browser.mjs is the in-browser check: the Standalone bundle, the
FUNC AGC tile on the virtual rig, and the Web Serial transport against a fake
IC-7300 (CI-V 16 12 read, set, read back, FM, per-mode re-read). It needs
playwright-core and a Chromium and is skipped without them.
"""

import json
import os
import shutil
import socket
import subprocess
import time
from pathlib import Path

import pytest
import requests

from conftest import wfweb_output_tail, ws_connect

ROOT = Path(__file__).resolve().parents[1]

AGC_FAST, AGC_MID, AGC_SLOW = 1, 2, 3
FM = 0x05
USB = 0x01
CW = 0x03


def _wait(predicate, timeout=20.0, what="condition", detail=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.25)
    pytest.fail(f"{what} not met within {timeout} s" + (f"\n{detail()}" if detail else ""))


def _agc_frames(mock, mark=0):
    return [bytes(f[4:-1]) for f in mock.civ_log[mark:] if bytes(f[4:6]) == bytes([0x16, 0x12])]


def _rigctl(port, command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        reader = s.makefile("r", encoding="ascii", errors="replace")
        s.sendall((command + "\n").encode())
        return reader.readline().rstrip("\n")


@pytest.fixture
def rig(wfweb_7300_instance, mock_radio_7300):
    """The IC-7300 mock in USB with AGC MID, the server aware of both, and a
    WebSocket client collecting every agc value the server pushes."""
    proc, web_port = wfweb_7300_instance
    base = f"http://127.0.0.1:{web_port + 1}/api/v1/radio"
    mock = mock_radio_7300
    mock.set_mode(USB)
    mock.agc = AGC_MID

    info = requests.get(base + "/info", timeout=2).json()
    info = info.get("info", info)
    assert info.get("hasAgc") is True, (
        "rigInfo has no AGC: the IC-7300.rig the server loaded has no AGC list "
        "(install the repo's rigs/*.rig)\n" + wfweb_output_tail(proc))

    def status():
        return requests.get(base + "/status", timeout=2).json()

    _wait(lambda: status().get("mode") == "USB", what="status reports USB")
    ws = ws_connect(web_port, proc)
    pushed = []

    def send(**cmd):
        ws.send(json.dumps(cmd))

    def drain(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            try:
                msg = json.loads(ws.recv(timeout=0.2))
            except TimeoutError:
                continue
            if isinstance(msg, dict) and "agc" in msg:
                pushed.append(msg["agc"])

    def wait_pushed(value, timeout=8.0, what=None):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            drain(0.3)
            if pushed and pushed[-1] == value:
                return
        pytest.fail(f"{what or f'agc {value} pushed'} not seen within {timeout} s; "
                    f"pushed {pushed}, mock agc={mock.agc}, 16 12 frames {_agc_frames(mock)[-6:]}")

    yield mock, send, status, wait_pushed, pushed
    try:
        ws.close()
    except Exception:
        pass
    mock.set_mode(USB)
    mock.agc = AGC_MID


def test_info_names_the_rigs_agc_settings(wfweb_7300_instance, rig):
    """agcModes is the rig file's list, in the rig's order, with its values."""
    _, web_port = wfweb_7300_instance
    info = requests.get(f"http://127.0.0.1:{web_port + 1}/api/v1/radio/info", timeout=2).json()
    info = info.get("info", info)
    assert info["agcModes"] == [
        {"num": 1, "name": "FAST"}, {"num": 2, "name": "MID"}, {"num": 3, "name": "SLOW"}]
    _, _, status, _, _ = rig
    _wait(lambda: status().get("agc") == AGC_MID, what="status reports AGC MID")


def test_set_agc_reaches_the_rig_and_comes_back(rig):
    """setAGC writes 16 12 <value>; the read-back pushes the rig's value."""
    mock, send, status, wait_pushed, _ = rig
    mark = len(mock.civ_log)
    send(cmd="setAGC", value=AGC_SLOW)
    _wait(lambda: mock.agc == AGC_SLOW, what="mock AGC SLOW")
    assert bytes([0x16, 0x12, AGC_SLOW]) in _agc_frames(mock, mark)
    wait_pushed(AGC_SLOW)
    _wait(lambda: status().get("agc") == AGC_SLOW, what="status follows AGC SLOW")

    send(cmd="setAGC", value=AGC_FAST)
    _wait(lambda: mock.agc == AGC_FAST, what="mock AGC FAST")
    wait_pushed(AGC_FAST)


def test_fm_keeps_fast_and_the_tile_hears_it(rig):
    """In FM the rig refuses the set and reads FAST: the read-back says so,
    or a page that showed the new setting at once would keep showing it."""
    mock, send, _, wait_pushed, _ = rig
    send(cmd="setMode", value="FM")
    _wait(lambda: mock._mode == FM, what="mock in FM")
    wait_pushed(AGC_FAST, what="FAST read in FM")
    send(cmd="setAGC", value=AGC_SLOW)
    time.sleep(0.6)
    wait_pushed(AGC_FAST, what="FAST still reported after the refused set")
    assert mock.agc == AGC_MID, "the mock took a set it should refuse in FM"


def test_mode_change_rereads_agc(rig):
    """The rig keeps AGC per mode, so a mode change reads it again rather
    than leaving the previous mode's setting on the tile."""
    mock, send, _, wait_pushed, _ = rig
    wait_pushed(AGC_MID, what="AGC MID read at the start")
    mock.agc = AGC_SLOW                 # what CW would have been left at
    mark = len(mock.civ_log)
    send(cmd="setMode", value="CW")
    _wait(lambda: mock._mode == CW, what="mock in CW")
    wait_pushed(AGC_SLOW, what="AGC read again after the mode change")
    assert _agc_frames(mock, mark), "no 16 12 read after the mode change"


def test_rigctld_agc_level_maps_through_the_names(rig, rigctld_7300_port):
    """hamlib RIG_AGC_FAST=2, SLOW=3, MEDIUM=5 against the rig's 1 / 3 / 2."""
    mock, send, status, _, _ = rig
    send(cmd="setAGC", value=AGC_SLOW)   # the read-back fills the server's cache
    _wait(lambda: _rigctl(rigctld_7300_port, "l AGC") == "3",
          what="rigctld reads SLOW as RIG_AGC_SLOW (3)",
          detail=lambda: f"l AGC -> {_rigctl(rigctld_7300_port, 'l AGC')}, status agc={status().get('agc')}")
    assert _rigctl(rigctld_7300_port, "L AGC 5") == "RPRT 0"
    _wait(lambda: mock.agc == AGC_MID, what="RIG_AGC_MEDIUM sets MID (2)")
    assert _rigctl(rigctld_7300_port, "L AGC 2") == "RPRT 0"
    _wait(lambda: mock.agc == AGC_FAST, what="RIG_AGC_FAST sets FAST (1)")
    # The IC-7300 has no AGC OFF on 16 12: refused, nothing sent.
    mark = len(mock.civ_log)
    assert _rigctl(rigctld_7300_port, "L AGC 0") != "RPRT 0"
    time.sleep(0.3)
    assert not [f for f in _agc_frames(mock, mark) if len(f) > 2]


def _node(script, *args, timeout=300, env=None):
    return subprocess.run(["node", str(ROOT / "tests" / script), *args],
                          check=False, capture_output=True, text=True, timeout=timeout,
                          env=None if env is None else {**os.environ, **env})


def test_agc_browser():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("agc_browser.mjs", "--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    result = _node("agc_browser.mjs")
    assert result.returncode == 0, result.stdout + result.stderr


def test_agc_browser_server_page(wfweb_7300_instance, rig):
    """The Server build's page on the IC-7300 mock: the tile, a tap the server
    confirms, and a Shared Station refusal that leaves the tile alone."""
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("agc_browser.mjs", "--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    _, web_port = wfweb_7300_instance
    result = _node("agc_browser.mjs", env={"WFWEB_AGC_URL": f"https://127.0.0.1:{web_port}/"})
    assert result.returncode == 0, result.stdout + result.stderr
