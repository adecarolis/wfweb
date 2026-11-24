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
    "+\\wait_morse", "\\wait_morse",
])
def test_command_without_result_labels_does_not_crash(wfweb_instance, rigctld_port, command):
    """wait_morse has no result labels; it used to index an empty prefix list and
    crash wfweb with SIGSEGV (get_clock too, before it was implemented). It must
    now report "not implemented"."""
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


def _poll_rigctl(port, command, predicate, timeout=20.0):
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


# ---------------------------------------------------------------------------
# set_clock / get_clock on the IC-7300 mock: 1A 05 00 94 date (YYYY MM DD),
# 00 95 time (HH MM), 00 96 UTC offset (HH MM sign), all BCD. The mock keeps
# what is written to a 1A 05 register and reads it back.
# ---------------------------------------------------------------------------

DATE_REG, TIME_REG, OFFSET_REG = 0x0094, 0x0095, 0x0096


def _bcd(*numbers):
    return bytes(int(str(n), 16) for n in numbers)


def _clock_regs(mock):
    return tuple(mock.settings.get(r) for r in (DATE_REG, TIME_REG, OFFSET_REG))


@pytest.mark.parametrize("argument, date, time_, offset", [
    # Hamlib's own form from a client: seconds and milliseconds, offset in hours
    ("2026-11-24T14:40:05.123+00", (20, 26, 11, 24), (14, 40), (0, 0, 0)),
    # no seconds; the offset as HHMM
    ("2026-01-02T03:04+0130", (20, 26, 1, 2), (3, 4), (1, 30, 0)),
    # negative offset with minutes, as +HH:MM
    ("2027-12-31T23:59:59-05:30", (20, 27, 12, 31), (23, 59), (5, 30, 1)),
])
def test_set_clock_sets_date_time_and_utc_offset(wfweb_7300_instance, mock_radio_7300, rigctld_7300_port,
                                                  argument, date, time_, offset):
    """The rig gets the date, the time (hours and minutes: it keeps no seconds) and
    the UTC offset, as Hamlib's Icom backend sends them."""
    assert _rigctl(rigctld_7300_port, f"\\set_clock {argument}") == ["RPRT 0"]
    want = (_bcd(*date), _bcd(*time_), _bcd(*offset))
    _poll_until(lambda: _clock_regs(mock_radio_7300) == want)


def test_set_clock_extended_form_echoes_the_command(wfweb_7300_instance, mock_radio_7300, rigctld_7300_port):
    lines = _rigctl(rigctld_7300_port, "+\\set_clock 2026-11-24T08:15:00+01")
    assert lines == ["set_clock: 2026-11-24T08:15:00+01", "RPRT 0"]
    _poll_until(lambda: _clock_regs(mock_radio_7300) == (_bcd(20, 26, 11, 24), _bcd(8, 15), _bcd(1, 0, 0)))


def test_set_clock_utc_uses_the_computer_clock(wfweb_7300_instance, mock_radio_7300, rigctld_7300_port):
    """'utc' (newer Hamlib) sets the computer's UTC time with a zero offset."""
    from datetime import datetime, timedelta, timezone
    before = datetime.now(timezone.utc)
    assert _rigctl(rigctld_7300_port, "\\set_clock utc") == ["RPRT 0"]
    candidates = {(_bcd(t.year // 100, t.year % 100, t.month, t.day), _bcd(t.hour, t.minute), _bcd(0, 0, 0))
                  for t in (before, before + timedelta(minutes=1))}
    _poll_until(lambda: _clock_regs(mock_radio_7300) in candidates)


@pytest.mark.parametrize("argument", [
    "garbage",
    "2026-11-24T14:40:05",            # no UTC offset
    "2026-02-30T10:00+00",            # no such date
    "2026-11-24T24:00+00",            # no such time
    "2026-11-24T10:00+15",            # offset out of range
])
def test_set_clock_refuses_a_bad_argument(wfweb_7300_instance, mock_radio_7300, rigctld_7300_port, argument):
    proc, _ = wfweb_7300_instance
    assert _rigctl(rigctld_7300_port, f"\\set_clock {argument}") == ["RPRT -1"]
    assert proc.poll() is None, "wfweb exited"


def test_get_clock_reads_the_rig_clock(wfweb_7300_instance, mock_radio_7300, rigctld_7300_port):
    """get_clock answers in Hamlib's format from the rig's date, time and offset
    (seconds 0, as Hamlib's Icom backend). A value not read yet is asked for and
    answered as a timeout, so the client retries."""
    mock_radio_7300.settings[DATE_REG] = _bcd(20, 30, 6, 7)
    mock_radio_7300.settings[TIME_REG] = _bcd(9, 41)
    mock_radio_7300.settings[OFFSET_REG] = _bcd(2, 0, 1)
    lines = _poll_rigctl(rigctld_7300_port, "\\get_clock",
                         lambda l: l == ["2030-06-07T09:41:00.000-02:00"], timeout=40.0)
    lines = _rigctl(rigctld_7300_port, "+\\get_clock")
    assert lines == ["get_clock:", "2030-06-07T09:41:00.000-02:00", "RPRT 0"]


def test_set_clock_then_get_clock_round_trip(wfweb_7300_instance, mock_radio_7300, rigctld_7300_port):
    assert _rigctl(rigctld_7300_port, "\\set_clock 2028-03-04T05:06:07+03") == ["RPRT 0"]
    _poll_rigctl(rigctld_7300_port, "\\get_clock",
                 lambda l: l == ["2028-03-04T05:06:00.000+03:00"], timeout=40.0)
