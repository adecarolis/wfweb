"""FT8: the click on an answer that lands while our own CQ is going out.

The decode of a station answering our CQ comes in at the top of our next
slot, just as the auto-CQ starts. The click on it used to leave the CQ on the
air (single click) or cut it and keep the slot silent (double click), so the
report went out a whole cycle later. Now the click takes the CQ's place in
the same slot while the cut report still decodes (digiTakeOverTx,
digiLateStartLimit). tests/ft8_takeover_browser.mjs runs it on two Standalone
virtual rigs: the report goes out in the CQ's slot, the other side decodes it
there and the QSO reaches the log; past the limit the CQ runs to its end and
the report waits a cycle.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_ft8_takeover_browser():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    script = str(ROOT / "tests" / "ft8_takeover_browser.mjs")
    probe = subprocess.run(["node", script, "--probe"], check=False, capture_output=True, text=True, timeout=30)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium")
    result = subprocess.run(["node", script], check=False, capture_output=True, text=True, timeout=480)
    assert result.returncode == 0, result.stdout + result.stderr
