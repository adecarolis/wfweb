"""Test frequency read and write via REST API."""

import requests


def test_get_frequency(rest_url):
    """GET /frequency should return frequency in Hz and MHz."""
    r = requests.get(f"{rest_url}/frequency", timeout=5)
    assert r.status_code == 200
    data = r.json()
    assert "hz" in data
    assert "mhz" in data
    assert data["hz"] > 0
    assert abs(data["mhz"] - data["hz"] / 1_000_000) < 0.001


def test_set_frequency(rest_url, poll):
    """PUT /frequency should change the frequency, visible on next GET."""
    target_hz = 7_074_000

    r = requests.put(
        f"{rest_url}/frequency",
        json={"hz": target_hz},
        timeout=5,
    )
    assert r.status_code == 202

    data = poll("frequency", lambda d: d.get("hz") == target_hz)
    assert data["hz"] == target_hz
    assert abs(data["mhz"] - 7.074) < 0.001

    # Restore
    requests.put(f"{rest_url}/frequency", json={"hz": 14_074_000}, timeout=5)
    poll("frequency", lambda d: d.get("hz") == 14_074_000)


def test_set_frequency_low_band(rest_url, poll):
    """Frequency SET should work for low HF bands (1.8 MHz)."""
    target_hz = 1_840_000

    r = requests.put(f"{rest_url}/frequency", json={"hz": target_hz}, timeout=5)
    assert r.status_code == 202

    data = poll("frequency", lambda d: d.get("hz") == target_hz)
    assert data["hz"] == target_hz

    # Restore
    requests.put(f"{rest_url}/frequency", json={"hz": 14_074_000}, timeout=5)
    poll("frequency", lambda d: d.get("hz") == 14_074_000)


def test_set_frequency_high_band(rest_url, poll):
    """Frequency SET should work for high HF bands (50 MHz)."""
    target_hz = 50_313_000

    r = requests.put(f"{rest_url}/frequency", json={"hz": target_hz}, timeout=5)
    assert r.status_code == 202

    data = poll("frequency", lambda d: d.get("hz") == target_hz)
    assert data["hz"] == target_hz

    # Restore
    requests.put(f"{rest_url}/frequency", json={"hz": 14_074_000}, timeout=5)
    poll("frequency", lambda d: d.get("hz") == 14_074_000)


def test_set_frequency_precise(rest_url, poll):
    """Frequency should preserve exact Hz precision through BCD encoding."""
    # 14.074.123 Hz — tests that all 10 BCD digits round-trip correctly
    target_hz = 14_074_123

    r = requests.put(f"{rest_url}/frequency", json={"hz": target_hz}, timeout=5)
    assert r.status_code == 202

    data = poll("frequency", lambda d: d.get("hz") == target_hz)
    assert data["hz"] == target_hz

    # Restore
    requests.put(f"{rest_url}/frequency", json={"hz": 14_074_000}, timeout=5)
    poll("frequency", lambda d: d.get("hz") == 14_074_000)


def test_set_frequency_missing_field(rest_url):
    """PUT /frequency with missing 'hz' field should return 400."""
    r = requests.put(
        f"{rest_url}/frequency",
        json={"mhz": 14.074},
        timeout=5,
    )
    assert r.status_code == 400
