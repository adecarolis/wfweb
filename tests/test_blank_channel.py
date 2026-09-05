"""A rig with nothing to report must yield stable null fields, not garbage.

An IC-705 sitting on a blank memory channel answers the frequency and mode
reads (25 00 / 26 00) with a single 0xFF data byte. Decoding that as BCD gave
"frequency": 165, "mode": "Unknown", "filter": 255 - or dropped the frequency
key from /status altogether, which a Python client turned into a KeyError.
"""

import requests


def test_blank_channel_reports_null(mock_radio, rest_url, poll):
    """0xFF replies surface as null in /status, /frequency and /mode."""
    mock_radio.blank_channel = True
    try:
        status = poll("status",
                      lambda d: "frequency" in d and d["frequency"] is None,
                      timeout=15.0)
        assert status["frequency"] is None
        assert "mode" in status and status["mode"] is None
        assert "filter" in status and status["filter"] is None

        r = requests.get(f"{rest_url}/frequency", timeout=5)
        assert r.status_code == 200
        assert r.json() == {"hz": None, "mhz": None}

        r = requests.get(f"{rest_url}/mode", timeout=5)
        assert r.status_code == 200
        assert r.json() == {"mode": None, "filter": None}
    finally:
        mock_radio.blank_channel = False

    # Leaving the blank channel brings the real values back for the tests
    # that follow (and proves the cache was not left stuck on the sentinel).
    status = poll("status",
                  lambda d: (d.get("frequency") or 0) > 0
                  and isinstance(d.get("mode"), str) and d.get("filter") in (1, 2, 3),
                  timeout=15.0)
    assert status["frequency"] == 14_074_000
    assert status["mode"] == "USB"
    # The sub receiver (VFO B on this cmd29 mock) is not on the fast poll; it
    # refreshes when its cache entry goes stale, 5-20 s after the sentinel
    # landed. Wait it out so test_vfo does not inherit a null.
    poll("vfo",
         lambda d: isinstance(d.get("vfoA"), int) and isinstance(d.get("vfoB"), int),
         timeout=30.0)
