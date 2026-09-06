"""A rig with nothing to report must yield stable null fields, not garbage.

An IC-705 sitting on a blank memory channel answers the frequency and mode
reads (25 00 / 26 00) with a single 0xFF data byte. Decoding that as BCD gave
"frequency": 165, "mode": "Unknown", "filter": 255 - or dropped the frequency
key from /status altogether, which a Python client turned into a KeyError.
"""

import requests


def _wait_for_live_values(poll):
    """Wait until the mock's real values are back in every cache we poisoned.

    Runs whether or not the assertions above it passed: a failure that left
    the session on the sentinel would hand every later test a null frequency.
    """
    status = poll("status",
                  lambda d: (d.get("frequency") or 0) > 0
                  and isinstance(d.get("mode"), str) and d.get("filter") in (1, 2, 3),
                  timeout=30.0)
    # The sub receiver (VFO B on this cmd29 mock) is not on the fast poll; it
    # refreshes when its cache entry goes stale, 5-20 s after the sentinel
    # landed. Wait it out so test_vfo does not inherit a null.
    poll("vfo",
         lambda d: isinstance(d.get("vfoA"), int) and isinstance(d.get("vfoB"), int),
         timeout=30.0)
    return status


def test_blank_channel_reports_null(mock_radio, rest_url, poll):
    """0xFF replies surface as null in /status, /frequency and /mode."""
    mock_radio.blank_channel = True
    try:
        # Frequency and mode are separate reads, so the sentinel lands in the
        # two caches a poll cycle apart (~6 s on a desktop, longer on the
        # emulated arm64 CI runner) and in either order. Wait for every field
        # before asserting on any of them, or a slow host fails on the one
        # still in flight. 30 s matches conftest's _wait_for_cache budget.
        status = poll("status",
                      lambda d: all(k in d and d[k] is None
                                    for k in ("frequency", "mode", "filter")),
                      timeout=30.0)
        assert status["frequency"] is None
        assert status["mode"] is None
        assert status["filter"] is None

        r = requests.get(f"{rest_url}/frequency", timeout=5)
        assert r.status_code == 200
        assert r.json() == {"hz": None, "mhz": None}

        r = requests.get(f"{rest_url}/mode", timeout=5)
        assert r.status_code == 200
        assert r.json() == {"mode": None, "filter": None}
    finally:
        mock_radio.blank_channel = False
        status = _wait_for_live_values(poll)

    # Leaving the blank channel brings the real values back (and proves the
    # cache was not left stuck on the sentinel).
    assert status["frequency"] == 14_074_000
    assert status["mode"] == "USB"
