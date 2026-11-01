"""The iOS microphone follows the voice PTT (#151).

With the mic open, WebKit on iOS plays the receiver several times quieter,
so on iOS MIC ON only arms the mic and the capture is open from a voice key
to its unkey. tests/mic_ptt_browser.mjs checks it on the Standalone virtual
rig with an iPhone user agent, and that a desktop keeps the mic open. It
needs playwright-core and a Chromium and is skipped without them.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _node(*args, timeout=180):
    return subprocess.run(["node", str(ROOT / "tests" / "mic_ptt_browser.mjs"), *args], cwd=ROOT,
                          check=False, capture_output=True, text=True, timeout=timeout)


def test_mic_follows_ptt_browser():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    result = _node()
    assert result.returncode == 0, result.stdout + result.stderr
