"""Test RX settings via REST API."""

import requests


def _safe_json(r):
    """Parse JSON response, returning {} for empty bodies."""
    return r.json() if r.text else {}


def test_get_rx(rest_url):
    """GET /rx should return 200 (fields may be absent before cache populates)."""
    r = requests.get(f"{rest_url}/rx", timeout=5)
    assert r.status_code == 200
    data = _safe_json(r)
    # If any RX fields are present, verify their types
    if "preamp" in data:
        assert isinstance(data["preamp"], int)
    if "nb" in data:
        assert isinstance(data["nb"], bool)
    if "nr" in data:
        assert isinstance(data["nr"], bool)


def test_set_nb(rest_url, poll):
    """PUT /rx with nb=true should enable noise blanker."""
    r = requests.put(f"{rest_url}/rx", json={"nb": True}, timeout=5)
    assert r.status_code == 202

    data = poll("rx", lambda d: d.get("nb") is True)
    assert data["nb"] is True

    # Restore
    requests.put(f"{rest_url}/rx", json={"nb": False}, timeout=5)
    poll("rx", lambda d: d.get("nb") is False)


def test_set_nr(rest_url, poll):
    """PUT /rx with nr=true should enable noise reduction."""
    r = requests.put(f"{rest_url}/rx", json={"nr": True}, timeout=5)
    assert r.status_code == 202

    data = poll("rx", lambda d: d.get("nr") is True)
    assert data["nr"] is True

    # Restore
    requests.put(f"{rest_url}/rx", json={"nr": False}, timeout=5)
    poll("rx", lambda d: d.get("nr") is False)
