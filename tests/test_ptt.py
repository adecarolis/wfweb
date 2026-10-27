"""Test PTT (Push-To-Talk) via REST API."""

import pytest
import requests


def _safe_json(r):
    """Parse JSON response, returning {} for empty bodies."""
    return r.json() if r.text else {}


def test_get_ptt(rest_url):
    """GET /ptt should return 200 (transmitting may be absent before first poll)."""
    r = requests.get(f"{rest_url}/ptt", timeout=5)
    assert r.status_code == 200
    data = _safe_json(r)
    if "transmitting" in data:
        assert isinstance(data["transmitting"], bool)


def test_ptt_on_off(rest_url, poll):
    """PUT /ptt should toggle transmit state."""
    # Key up
    r = requests.put(f"{rest_url}/ptt", json={"transmitting": True}, timeout=5)
    assert r.status_code == 202

    data = poll("ptt", lambda d: d.get("transmitting") is True, timeout=5.0)
    assert data["transmitting"] is True

    # Key down
    r = requests.put(f"{rest_url}/ptt", json={"transmitting": False}, timeout=5)
    assert r.status_code == 202

    data = poll("ptt", lambda d: d.get("transmitting") is False, timeout=5.0)
    assert data["transmitting"] is False


def test_ptt_released_when_keying_client_disconnects(wfweb_instance, poll):
    """A browser that keyed the rig and then vanished must not leave it
    transmitting (#144: a latched touch PTT holds an over for minutes)."""
    import json
    from conftest import ws_connect
    proc, web_port = wfweb_instance

    ws = ws_connect(web_port, proc)
    ws.send(json.dumps({"cmd": "setPTT", "value": True}))
    poll("ptt", lambda d: d.get("transmitting") is True, timeout=5.0)

    # Drop the socket without an unkey, the way a phone losing its network does.
    ws.socket.close()
    ws.close()

    data = poll("ptt", lambda d: d.get("transmitting") is False, timeout=5.0)
    assert data["transmitting"] is False
