"""Test mode read and write via REST API."""

import time

import requests


def test_get_mode(rest_url):
    """GET /mode should return current mode and filter."""
    r = requests.get(f"{rest_url}/mode", timeout=5)
    assert r.status_code == 200
    data = r.json()
    assert "mode" in data
    assert "filter" in data
    # Mock defaults to USB, filter 1
    assert data["mode"] == "USB"
    assert data["filter"] == 1


def test_set_mode(rest_url):
    """PUT /mode should change the mode."""
    # Set to LSB
    r = requests.put(
        f"{rest_url}/mode",
        json={"mode": "LSB"},
        timeout=5,
    )
    assert r.status_code == 202

    # Wait for round-trip
    time.sleep(1.0)

    # Read back
    r = requests.get(f"{rest_url}/mode", timeout=5)
    assert r.status_code == 200
    data = r.json()
    assert data["mode"] == "LSB"

    # Restore USB
    requests.put(f"{rest_url}/mode", json={"mode": "USB"}, timeout=5)
    time.sleep(0.5)
