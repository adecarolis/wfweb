"""Test frequency read and write via REST API."""

import time

import requests


def test_get_frequency(rest_url):
    """GET /frequency should return the mock's default frequency."""
    r = requests.get(f"{rest_url}/frequency", timeout=5)
    assert r.status_code == 200
    data = r.json()
    assert "hz" in data
    assert "mhz" in data
    # Mock defaults to 14.074 MHz
    assert data["hz"] == 14_074_000
    assert abs(data["mhz"] - 14.074) < 0.001


def test_set_frequency(rest_url):
    """PUT /frequency should change the frequency, visible on next GET."""
    target_hz = 7_074_000

    # Set frequency
    r = requests.put(
        f"{rest_url}/frequency",
        json={"hz": target_hz},
        timeout=5,
    )
    assert r.status_code == 202

    # Wait for the command to round-trip through cachingQueue -> mock -> cache update
    time.sleep(1.0)

    # Read back
    r = requests.get(f"{rest_url}/frequency", timeout=5)
    assert r.status_code == 200
    data = r.json()
    assert data["hz"] == target_hz

    # Restore default for other tests
    requests.put(f"{rest_url}/frequency", json={"hz": 14_074_000}, timeout=5)
    time.sleep(0.5)
