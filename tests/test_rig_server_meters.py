"""wfweb as a rig server (--no-web) must not poll the TX meters.

initPeriodicPolling() adds the power, SWR and ALC meter reads to the periodic
poll for the web UI. In --no-web mode nothing on the server reads them (they
are used by webServer and rigctld only), but the rig answers every read on the
CI-V stream that the rig server forwards to its LAN clients: a wfview client
then switches its main meter between S and PWR several times a second while
receiving.

Both tests run wfweb against the PTY mock radio and count the CI-V reads it
sends. The web mode test keeps the check honest: there the reads must be sent.
"""

import os
import socket
import subprocess
import time

import pytest

from conftest import WFWEB_BIN, _find_free_port, wfweb_output_tail
from mock_civ import (CMD_METER, SUB_ALC_METER, SUB_POWER_METER, SUB_S_METER,
                      SUB_SWR_METER)
from mock_serial_radio import SerialMockRadio

TX_METERS = (SUB_POWER_METER, SUB_SWR_METER, SUB_ALC_METER)
RIG = 0x94  # IC-7300, the rig of the report


def _free_udp_ports(count: int) -> list[int]:
    """Distinct free UDP ports (the sockets are held until all are picked)."""
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


def _meter_reads(mock: SerialMockRadio, sub: int) -> int:
    """Meter reads received by the mock: FE FE <rig> <ctl> [29 <rx>] 15 <sub> FD
    (dual-receiver rigs are asked with the 0x29 receiver prefix)."""
    count = 0
    for f in list(mock.civ_log):
        body = f[4:-1]
        if body[:1] == b"\x29":
            body = body[2:]
        if len(body) == 2 and body[0] == CMD_METER and body[1] == sub:
            count += 1
    return count


def _poll_meters(tmp_path, mode_args: list[str]) -> dict[int, int]:
    """Run wfweb against a fresh mock radio until the periodic poll has run
    for a few seconds; return the meter reads per sub-command."""
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")

    mock = SerialMockRadio(radio_addr=RIG)
    mock.start()

    control, civ, audio = _free_udp_ports(3)
    settings = tmp_path / "wfweb.conf"
    # [Radios] must be present: without it wfweb recreates the radio config
    # on the first start and resets [Server] (ports) to the defaults.
    settings.write_text(
        "[General]\nAudioSystem=0\nManufacturer=0\n\n"
        "[Radios]\n1\\AudioInput=default\n1\\AudioOutput=default\n"
        "1\\RigCIVuInt=0\n1\\RigName=<NONE>\n1\\SerialPortBaud=115200\n"
        "1\\SerialPortRadio=auto\nsize=1\n\n"
        f"[Server]\nServerEnabled=true\nServerControlPort={control}\n"
        f"ServerCivPort={civ}\nServerAudioPort={audio}\n"
    )
    log = tmp_path / "wfweb.log"
    env = os.environ.copy()
    env["HOME"] = str(tmp_path)
    env["XDG_CONFIG_HOME"] = str(tmp_path / "config")
    env["XDG_DATA_HOME"] = str(tmp_path / "data")
    env["WFWEB_NO_TUI"] = "1"

    proc = subprocess.Popen(
        [str(WFWEB_BIN), "-s", str(settings), "-l", str(log),
         "--serial-port", mock.port_path, "--civ", hex(RIG), "--no-rigctld",
         *mode_args],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, env=env)
    proc.output_path = str(log)
    try:
        deadline = time.monotonic() + 30
        while _meter_reads(mock, SUB_S_METER) < 5:
            if proc.poll() is not None or time.monotonic() > deadline:
                pytest.fail("wfweb did not start polling the mock radio:\n"
                            + wfweb_output_tail(proc))
            time.sleep(0.2)
        time.sleep(5)  # the periodic poll keeps running
        return {sub: _meter_reads(mock, sub) for sub in (SUB_S_METER, *TX_METERS)}
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)
        mock.stop()


def test_rig_server_does_not_poll_tx_meters(tmp_path):
    reads = _poll_meters(tmp_path, ["--no-web"])
    assert reads[SUB_S_METER] > 0
    assert all(reads[sub] == 0 for sub in TX_METERS), \
        f"--no-web polled the TX meters: {reads}"


def test_web_server_polls_tx_meters(tmp_path):
    reads = _poll_meters(tmp_path, ["-p", str(_find_free_port())])
    assert all(reads[sub] > 0 for sub in TX_METERS), \
        f"the web server did not poll the TX meters: {reads}"
