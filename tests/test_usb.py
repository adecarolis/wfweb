"""Test wfweb via USB/serial (PTY) connection.

These tests exercise the same REST API as the LAN tests, but wfweb is
connected to a PTY-based mock radio via QSerialPort instead of UDP.
This exercises the commhandler.cpp serial code path, which is what the
majority of users (USB-connected radios) actually use.

The LAN tests verify REST API behavior in detail. These USB tests focus
on verifying the serial transport works end-to-end, so they cover the
key operations without repeating every edge case.
"""

import requests


# ---------------------------------------------------------------------------
# Connection
# ---------------------------------------------------------------------------


def test_usb_connects(usb_rest_url):
    """wfweb should connect to the radio via serial PTY."""
    r = requests.get(f"{usb_rest_url}/info", timeout=5)
    assert r.status_code == 200
    info = r.json()
    assert info["connected"] is True


def test_usb_status(usb_rest_url):
    """GET /status should return frequency and mode over serial."""
    r = requests.get(f"{usb_rest_url}/status", timeout=5)
    assert r.status_code == 200
    status = r.json()
    assert "frequency" in status
    assert status["frequency"] > 0
    assert "mode" in status


# ---------------------------------------------------------------------------
# Frequency over serial
# ---------------------------------------------------------------------------


def test_usb_get_frequency(usb_rest_url):
    """GET /frequency should work via serial connection."""
    r = requests.get(f"{usb_rest_url}/frequency", timeout=5)
    assert r.status_code == 200
    data = r.json()
    assert "hz" in data
    assert data["hz"] > 0


def test_usb_set_frequency(usb_rest_url, usb_poll):
    """PUT /frequency should round-trip through the serial CI-V path."""
    target_hz = 21_074_000

    r = requests.put(
        f"{usb_rest_url}/frequency",
        json={"hz": target_hz},
        timeout=5,
    )
    assert r.status_code == 202

    data = usb_poll("frequency", lambda d: d.get("hz") == target_hz)
    assert data["hz"] == target_hz

    # Restore
    requests.put(f"{usb_rest_url}/frequency", json={"hz": 14_074_000}, timeout=5)
    usb_poll("frequency", lambda d: d.get("hz") == 14_074_000)


# ---------------------------------------------------------------------------
# Mode over serial
# ---------------------------------------------------------------------------


def test_usb_set_mode(usb_rest_url, usb_poll):
    """PUT /mode should change the mode via serial CI-V."""
    r = requests.put(f"{usb_rest_url}/mode", json={"mode": "CW"}, timeout=5)
    assert r.status_code == 202

    data = usb_poll("mode", lambda d: d.get("mode") == "CW")
    assert data["mode"] == "CW"

    # Restore
    requests.put(f"{usb_rest_url}/mode", json={"mode": "USB"}, timeout=5)
    usb_poll("mode", lambda d: d.get("mode") == "USB")


# ---------------------------------------------------------------------------
# Gains over serial (regression test for BCD encoding via serial path)
# ---------------------------------------------------------------------------


def test_usb_set_gain(usb_rest_url, usb_poll):
    """PUT /gains should work via serial — tests BCD encoding on the serial path."""
    requests.put(f"{usb_rest_url}/gains", json={"afGain": 200}, timeout=5)
    data = usb_poll("gains", lambda d: d.get("afGain") == 200)
    assert data["afGain"] == 200

    # Restore
    requests.put(f"{usb_rest_url}/gains", json={"afGain": 128}, timeout=5)
    usb_poll("gains", lambda d: d.get("afGain") == 128)


# ---------------------------------------------------------------------------
# CI-V log inspection (serial-specific: verify raw frames on the wire)
# ---------------------------------------------------------------------------


def test_usb_civ_frames_logged(usb_rest_url, serial_mock):
    """The serial mock should log CI-V frames received from wfweb.

    This verifies that wfweb is sending properly framed CI-V over the
    serial port (FE FE <to> <from> <cmd> ... FD).
    """
    # wfweb has been polling, so the log should have entries
    assert len(serial_mock.civ_log) > 0

    # Every logged frame should be valid CI-V
    for frame in serial_mock.civ_log:
        assert frame[:2] == b"\xfe\xfe", f"Bad preamble: {frame.hex()}"
        assert frame[-1:] == b"\xfd", f"Bad terminator: {frame.hex()}"
        assert len(frame) >= 6, f"Frame too short: {frame.hex()}"
