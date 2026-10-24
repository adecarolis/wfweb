"""Test the Hamlib rigctld emulation over TCP."""

import re
import socket
import time

import pytest


def _rigctl(port, command, timeout=5.0):
    """Send one rigctld command on a fresh connection, return the reply lines.

    Extended (+) and plain commands both end with an RPRT line when they fail;
    a successful plain get has no RPRT, so stop after the first line then.
    """
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


def test_get_freq(wfweb_instance, rigctld_port):
    """Sanity check: a regular get answers with a frequency and RPRT 0."""
    lines = _rigctl(rigctld_port, "+\\get_freq")
    assert lines[0] == "get_freq:"
    assert lines[1].startswith("Frequency: ")
    assert lines[-1] == "RPRT 0"


@pytest.mark.parametrize("command", [
    "+\\get_clock", "\\get_clock",
    "+\\wait_morse", "\\wait_morse",
])
def test_command_without_result_labels_does_not_crash(wfweb_instance, rigctld_port, command):
    """get_clock and wait_morse have no result labels; they used to index an empty
    prefix list and crash wfweb with SIGSEGV. They must now report "not implemented"."""
    proc, _ = wfweb_instance
    lines = _rigctl(rigctld_port, command)
    assert lines and lines[-1] == "RPRT -4"
    assert proc.poll() is None, "wfweb exited"


def test_stop_morse_still_accepted(wfweb_instance, rigctld_port):
    """stop_morse has no result labels either but must keep working."""
    proc, _ = wfweb_instance
    lines = _rigctl(rigctld_port, "+\\stop_morse")
    assert lines[-1] == "RPRT 0"
    assert proc.poll() is None, "wfweb exited"



# ---------------------------------------------------------------------------
# IC-7300 flavour (#147): no command 29, the operating VFO lives in the
# 25 00 / 26 00 slots. Until something selected a VFO the server's state was
# vfoUnknown and rigctld fell back to the plain set slots: a read answered
# the last value *set* (0 before any), a PKTUSB set went out as 06 without
# the data byte, and get_vfo said None.
# ---------------------------------------------------------------------------


def _poll_rigctl(port, command, predicate, timeout=10.0):
    """Repeat a rigctld get until predicate(lines) holds (the rig is polled)."""
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        lines = _rigctl(port, command)
        if predicate(lines):
            return lines
        time.sleep(0.3)
    pytest.fail(f"{command!r} never satisfied the predicate, last reply {lines}")


def _poll_until(predicate, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.2)
    pytest.fail("condition not met in time")


def test_get_freq_reports_the_dial_in_integer_hz(wfweb_7300_instance, mock_radio_7300, rigctld_7300_port):
    """f follows the dial (the polled slot, not the last set) and prints the
    integer Hamlib prints; pat parses it with Atoi and rejected '160000.000000'."""
    mock_radio_7300.set_frequency(7_102_500)
    lines = _poll_rigctl(rigctld_7300_port, "f", lambda l: l and l[0] == "7102500")
    assert re.fullmatch(r"\d+", lines[0])
    mock_radio_7300.set_frequency(14_074_000)
    _poll_rigctl(rigctld_7300_port, "f", lambda l: l and l[0] == "14074000")


def test_get_vfo_names_the_operating_vfo(wfweb_7300_instance, rigctld_7300_port):
    """v answers VFOA on a server where no client has selected a VFO yet."""
    assert _rigctl(rigctld_7300_port, "v") == ["VFOA"]


def test_set_mode_pktusb_puts_the_rig_in_data_mode(wfweb_7300_instance, mock_radio_7300, rigctld_7300_port):
    """M PKTUSB goes out as 26 00 <USB> <data=1> <filter>, so WSJT-X / pat can
    put the rig in USB-D through wfweb; m reads PKTUSB back from the poll."""
    assert _rigctl(rigctld_7300_port, "M PKTUSB 0") == ["RPRT 0"]
    _poll_until(lambda: mock_radio_7300.data_mode == 1 and mock_radio_7300._mode == 0x01)
    _poll_rigctl(rigctld_7300_port, "m", lambda l: l and l[0] == "PKTUSB")

    assert _rigctl(rigctld_7300_port, "M USB 0") == ["RPRT 0"]
    _poll_until(lambda: mock_radio_7300.data_mode == 0)
    _poll_rigctl(rigctld_7300_port, "m", lambda l: l and l[0] == "USB")


def test_quit_is_acknowledged(wfweb_7300_instance, rigctld_7300_port):
    """q answers RPRT 0 before the socket closes (Hamlib's close waits for it)."""
    with socket.create_connection(("127.0.0.1", rigctld_7300_port), timeout=5) as s:
        s.sendall(b"q\n")
        buf = b""
        while True:
            chunk = s.recv(64)
            if not chunk:
                break
            buf += chunk
    assert buf == b"RPRT 0\n"
