"""Test that wfweb connects to the mock radio and reports status."""

import requests


def test_wfweb_connects(rest_url):
    """wfweb should report connected=true with IC-7610 model."""
    r = requests.get(f"{rest_url}/info", timeout=5)
    assert r.status_code == 200
    info = r.json()
    assert info["connected"] is True
    assert "7610" in info.get("model", "")


def test_wfweb_reports_status(rest_url):
    """GET /status should return 200 with frequency and mode fields."""
    r = requests.get(f"{rest_url}/status", timeout=5)
    assert r.status_code == 200
    status = r.json()
    # These fields appear once the cache is populated (conftest waits for this)
    assert "frequency" in status
    assert status["frequency"] > 0
    assert "mode" in status


def test_radio_info_has_modes(rest_url):
    """The info endpoint should list available modes."""
    r = requests.get(f"{rest_url}/info", timeout=5)
    info = r.json()
    modes = info.get("modes", [])
    assert len(modes) > 0
    assert "USB" in modes
    assert "LSB" in modes
