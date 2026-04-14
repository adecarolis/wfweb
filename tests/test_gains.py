"""Test gain read and write via REST API.

Gain values are encoded as BCD over CI-V using QVariant::fromValue<ushort>().
These tests guard against the known regression where <uchar> was used instead,
which would corrupt gain values during BCD encoding.
"""

import requests


def test_get_gains(rest_url, poll):
    """GET /gains should return gain fields once cache is populated."""
    # Gains may not be cached until the first poll cycle completes.
    # Trigger a SET to populate at least one field, then verify GET.
    requests.put(f"{rest_url}/gains", json={"afGain": 128}, timeout=5)
    data = poll("gains", lambda d: "afGain" in d)
    assert isinstance(data["afGain"], int)


def test_set_af_gain(rest_url, poll):
    """PUT /gains with afGain should update AF gain."""
    r = requests.put(f"{rest_url}/gains", json={"afGain": 180}, timeout=5)
    assert r.status_code == 202

    data = poll("gains", lambda d: d.get("afGain") == 180)
    assert data["afGain"] == 180

    # Restore
    requests.put(f"{rest_url}/gains", json={"afGain": 128}, timeout=5)
    poll("gains", lambda d: d.get("afGain") == 128)


def test_set_rf_power(rest_url, poll):
    """PUT /gains with rfPower should update RF power."""
    r = requests.put(f"{rest_url}/gains", json={"rfPower": 50}, timeout=5)
    assert r.status_code == 202

    data = poll("gains", lambda d: d.get("rfPower") == 50)
    assert data["rfPower"] == 50

    # Restore
    requests.put(f"{rest_url}/gains", json={"rfPower": 100}, timeout=5)
    poll("gains", lambda d: d.get("rfPower") == 100)


def test_set_squelch(rest_url, poll):
    """PUT /gains with squelch should update squelch level."""
    r = requests.put(f"{rest_url}/gains", json={"squelch": 40}, timeout=5)
    assert r.status_code == 202

    data = poll("gains", lambda d: d.get("squelch") == 40)
    assert data["squelch"] == 40

    # Restore
    requests.put(f"{rest_url}/gains", json={"squelch": 0}, timeout=5)
    poll("gains", lambda d: d.get("squelch") == 0)


def test_set_multiple_gains(rest_url, poll):
    """PUT /gains with multiple fields should update all of them."""
    r = requests.put(
        f"{rest_url}/gains",
        json={"afGain": 200, "rfPower": 75},
        timeout=5,
    )
    assert r.status_code == 202

    data = poll("gains", lambda d: d.get("afGain") == 200 and d.get("rfPower") == 75)
    assert data["afGain"] == 200
    assert data["rfPower"] == 75

    # Restore
    requests.put(f"{rest_url}/gains", json={"afGain": 128, "rfPower": 100}, timeout=5)
    poll("gains", lambda d: d.get("afGain") == 128 and d.get("rfPower") == 100)


def test_gain_bcd_round_trip(rest_url, poll):
    """Gain values should survive BCD encoding round-trip without corruption.

    This is the regression test for the ushort vs uchar bug: if gains are
    encoded as uchar, BCD encoding of values like 255 produces incorrect
    bytes, and the radio applies the wrong gain.
    """
    for value in (0, 1, 128, 200, 255):
        requests.put(f"{rest_url}/gains", json={"afGain": value}, timeout=5)
        data = poll("gains", lambda d, v=value: d.get("afGain") == v)
        assert data["afGain"] == value, f"BCD round-trip failed for afGain={value}"

    # Restore
    requests.put(f"{rest_url}/gains", json={"afGain": 128}, timeout=5)
    poll("gains", lambda d: d.get("afGain") == 128)
