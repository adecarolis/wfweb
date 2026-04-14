"""Test mode read and write via REST API."""

import requests
import pytest


def test_get_mode(rest_url):
    """GET /mode should return current mode and filter."""
    r = requests.get(f"{rest_url}/mode", timeout=5)
    assert r.status_code == 200
    data = r.json()
    assert "mode" in data
    assert "filter" in data
    assert isinstance(data["mode"], str)
    assert data["filter"] in (1, 2, 3)


def test_set_mode_lsb(rest_url, poll):
    """PUT /mode should change to LSB."""
    r = requests.put(f"{rest_url}/mode", json={"mode": "LSB"}, timeout=5)
    assert r.status_code == 202

    data = poll("mode", lambda d: d.get("mode") == "LSB")
    assert data["mode"] == "LSB"

    # Restore
    requests.put(f"{rest_url}/mode", json={"mode": "USB"}, timeout=5)
    poll("mode", lambda d: d.get("mode") == "USB")


def test_set_mode_cw(rest_url, poll):
    """PUT /mode should change to CW."""
    r = requests.put(f"{rest_url}/mode", json={"mode": "CW"}, timeout=5)
    assert r.status_code == 202

    data = poll("mode", lambda d: d.get("mode") == "CW")
    assert data["mode"] == "CW"

    # Restore
    requests.put(f"{rest_url}/mode", json={"mode": "USB"}, timeout=5)
    poll("mode", lambda d: d.get("mode") == "USB")


def test_set_mode_am(rest_url, poll):
    """PUT /mode should change to AM."""
    r = requests.put(f"{rest_url}/mode", json={"mode": "AM"}, timeout=5)
    assert r.status_code == 202

    data = poll("mode", lambda d: d.get("mode") == "AM")
    assert data["mode"] == "AM"

    # Restore
    requests.put(f"{rest_url}/mode", json={"mode": "USB"}, timeout=5)
    poll("mode", lambda d: d.get("mode") == "USB")


def test_set_mode_with_filter(rest_url, poll):
    """PUT /mode with explicit filter should set both mode and filter."""
    r = requests.put(
        f"{rest_url}/mode",
        json={"mode": "CW", "filter": 2},
        timeout=5,
    )
    assert r.status_code == 202

    data = poll("mode", lambda d: d.get("mode") == "CW" and d.get("filter") == 2)
    assert data["mode"] == "CW"
    assert data["filter"] == 2

    # Restore
    requests.put(f"{rest_url}/mode", json={"mode": "USB", "filter": 1}, timeout=5)
    poll("mode", lambda d: d.get("mode") == "USB")


def test_set_mode_defaults_filter_to_fil1(rest_url, poll):
    """When filter is omitted from PUT /mode, it should default to FIL1.

    This guards against the modeInfo.filter regression where the default
    was not explicitly set, causing unpredictable filter selection.
    """
    # First set to CW with FIL2 to establish non-default state
    requests.put(f"{rest_url}/mode", json={"mode": "CW", "filter": 2}, timeout=5)
    poll("mode", lambda d: d.get("mode") == "CW" and d.get("filter") == 2)

    # Now set LSB without specifying filter
    requests.put(f"{rest_url}/mode", json={"mode": "LSB"}, timeout=5)
    data = poll("mode", lambda d: d.get("mode") == "LSB")
    assert data["filter"] == 1, "Filter should default to FIL1 when not specified"

    # Restore
    requests.put(f"{rest_url}/mode", json={"mode": "USB"}, timeout=5)
    poll("mode", lambda d: d.get("mode") == "USB")


def test_set_mode_missing_field(rest_url):
    """PUT /mode with missing 'mode' field should return 400."""
    r = requests.put(
        f"{rest_url}/mode",
        json={"filter": 1},
        timeout=5,
    )
    assert r.status_code == 400
