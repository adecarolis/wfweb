"""Adaptive RX jitter buffer (resources/web-shared/rx-jitter.js).

tests/rx_jitter_sim.mjs drives the buffer through synthetic jitter, stalls,
clock drift and TX gaps under node and checks that bumps stop once it has
adapted, that latency comes back down afterwards, and that the output never
has a hard discontinuity.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_rx_jitter_sim():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = subprocess.run(
        ["node", str(ROOT / "tests" / "rx_jitter_sim.mjs")],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_server_serves_rx_jitter(wfweb_instance):
    """The server build's page links rx-jitter.js and the qrc alias serves it."""
    import requests
    import urllib3

    urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    _, web_port = wfweb_instance
    pages = {}
    for scheme in ("https", "http"):
        try:
            for name in ("", "rx-jitter.js"):
                r = requests.get(f"{scheme}://127.0.0.1:{web_port}/{name}", timeout=5, verify=False)
                assert r.status_code == 200, (scheme, name, r.status_code)
                pages[name] = r.text
            break
        except requests.exceptions.ConnectionError:
            continue
    assert pages, "web port unreachable over https and http"
    assert 'src="rx-jitter.js"' in pages[""]
    assert "class WfJitterBuffer" in pages["rx-jitter.js"]
