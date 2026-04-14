"""Test TX settings via REST API."""

import requests


def _safe_json(r):
    """Parse JSON response, returning {} for empty bodies."""
    return r.json() if r.text else {}


def test_get_tx(rest_url):
    """GET /tx should return 200 (fields may be absent before cache populates)."""
    r = requests.get(f"{rest_url}/tx", timeout=5)
    assert r.status_code == 200
    data = _safe_json(r)
    if "split" in data:
        assert isinstance(data["split"], bool)


def test_set_split(rest_url, poll):
    """PUT /tx with split=true should enable split mode.

    Related to GitHub issue #20: split/VFO control failures after
    LAN power-up. This tests the basic split toggle works.
    """
    r = requests.put(f"{rest_url}/tx", json={"split": True}, timeout=5)
    assert r.status_code == 202

    data = poll("tx", lambda d: d.get("split") is True)
    assert data["split"] is True

    # Restore
    requests.put(f"{rest_url}/tx", json={"split": False}, timeout=5)
    poll("tx", lambda d: d.get("split") is False)
