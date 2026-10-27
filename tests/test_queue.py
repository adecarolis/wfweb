"""cachingQueue: a read the rig never answers is backed off.

getCache() re-asks every value missing from the cache on each status build,
as an immediate read. The IC-7300 mock ACKs (FB) the reads it does not
implement, which carries no value, so those reads used to be asked again on
every status build for the life of the server: ~30 of them queued at
immediate priority, 70 ms each on LAN, which starved the compressor read and
put every set 2 s behind (6 s+ on the macOS CI runner, 2026-10-27). The queue
now counts a read sent again with neither a value nor an NG since its last
ask as a refusal and, like a read the rig answers FA, stops asking after
CACHE_NAK_LIMIT (3) for CACHE_NAK_RETRY_SECS (30).
"""

import time

import requests

UNANSWERED = bytes([0x16, 0x47])  # BK-IN status: the mock has no handler
ANSWERED = bytes([0x15, 0x02])    # S-meter: the mock answers it


def test_unanswered_read_backs_off_while_answered_polls_go_on(wfweb_7300_instance, mock_radio_7300):
    proc, web_port = wfweb_7300_instance
    mock = mock_radio_7300
    url = f"http://127.0.0.1:{web_port + 1}/api/v1/radio/status"
    mark = len(mock.civ_log)
    deadline = time.monotonic() + 8.0
    while time.monotonic() < deadline:
        requests.get(url, timeout=2)
        time.sleep(0.5)
    frames = [bytes(f[4:-1]) for f in mock.civ_log[mark:]]
    unanswered = sum(1 for f in frames if f == UNANSWERED)
    answered = sum(1 for f in frames if f == ANSWERED)
    # 16 status builds: without the backoff the read goes out on each of them.
    assert unanswered <= 4, f"{unanswered} reads of 16 47 in 8 s: not backed off"
    assert answered >= 5, f"{answered} reads of 15 02 in 8 s: polling stalled"
