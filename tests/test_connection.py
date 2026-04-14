"""Test connection, info, and status endpoints."""

import requests


def test_wfweb_connects(rest_url):
    """wfweb should report connected=true with IC-7610 model."""
    r = requests.get(f"{rest_url}/info", timeout=5)
    assert r.status_code == 200
    info = r.json()
    assert info["connected"] is True
    assert "7610" in info.get("model", "")


def test_info_has_modes(rest_url):
    """Info should list available modes including USB and LSB."""
    r = requests.get(f"{rest_url}/info", timeout=5)
    info = r.json()
    modes = info.get("modes", [])
    assert len(modes) > 0
    assert "USB" in modes
    assert "LSB" in modes


def test_info_has_filters(rest_url):
    """Info should list available filters with num and name."""
    r = requests.get(f"{rest_url}/info", timeout=5)
    info = r.json()
    filters = info.get("filters", [])
    assert len(filters) > 0
    # Each filter should have a num and a name
    for f in filters:
        assert "num" in f
        assert "name" in f


def test_status_has_core_fields(rest_url):
    """GET /status should return frequency and mode (populated by conftest)."""
    r = requests.get(f"{rest_url}/status", timeout=5)
    assert r.status_code == 200
    status = r.json()

    # These are guaranteed populated (conftest waits for them)
    assert "frequency" in status
    assert status["frequency"] > 0
    assert "mode" in status
    assert isinstance(status["mode"], str)


def test_combined_endpoint(rest_url):
    """GET /api/v1/radio should return both info and status."""
    # Use the base URL without a sub-path
    r = requests.get(rest_url, timeout=5)
    assert r.status_code == 200
    data = r.json()

    assert "info" in data
    assert "status" in data
    assert data["info"]["connected"] is True
    assert data["status"]["frequency"] > 0
