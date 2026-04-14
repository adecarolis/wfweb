"""Test VFO operations via REST API.

Related to GitHub issue #20: Split mode / VFO control not working
after LAN power-up.
"""

import requests


def test_get_vfo(rest_url):
    """GET /vfo should return VFO A and B frequencies."""
    r = requests.get(f"{rest_url}/vfo", timeout=5)
    assert r.status_code == 200
    data = r.json()

    assert "vfoA" in data
    assert "vfoB" in data
    assert isinstance(data["vfoA"], int)
    assert isinstance(data["vfoB"], int)
