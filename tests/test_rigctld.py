"""Test the Hamlib rigctld emulation over TCP."""

import socket

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
