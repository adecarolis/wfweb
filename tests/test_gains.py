"""Test gain read and write via REST API.

Gain values are encoded as BCD over CI-V using QVariant::fromValue<ushort>().
These tests guard against the known regression where <uchar> was used instead,
which would corrupt gain values during BCD encoding.
"""

import time

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


# IC-7610 SET-mode registers (rigs/IC-7610.rig)
REG_USB_MOD_LEVEL = 0x0089
REG_LAN_MOD_LEVEL = 0x0090


def test_mic_gain_is_the_lan_mod_level_on_a_lan_rig(rest_url, poll, mock_radio):
    """A LAN rig takes our TX audio on its LAN input, so micGain is that
    input's level.  The USB one, which it used to write, does nothing there
    (#158)."""
    mock_radio.settings.pop(REG_USB_MOD_LEVEL, None)
    r = requests.put(f"{rest_url}/gains", json={"micGain": 150}, timeout=5)
    assert r.status_code == 202
    poll("gains", lambda d: d.get("micGain") == 150)
    # Wait on the mock itself: a REST poll here only adds reads to the queue
    # the set is waiting in (seconds on the macOS runner).
    deadline = time.monotonic() + 30.0
    while mock_radio.settings.get(REG_LAN_MOD_LEVEL) != bytes([0x01, 0x50]):
        assert time.monotonic() < deadline, f"LAN mod level never written: {mock_radio.settings}"
        time.sleep(0.1)
    assert REG_USB_MOD_LEVEL not in mock_radio.settings
    # Read back from the rig, not only from what we queued.
    mock_radio.settings[REG_LAN_MOD_LEVEL] = bytes([0x00, 0x77])
    poll("gains", lambda d: d.get("micGain") == 77, timeout=30.0)
    requests.put(f"{rest_url}/gains", json={"micGain": 128}, timeout=5)
