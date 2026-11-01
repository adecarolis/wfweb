"""Recorder: the mic is kept only while it is what goes on the air.

tests/recorder_browser.mjs runs the Standalone bundle on its virtual rig with
a fake 1500 Hz mic and records three overs: CW (the TONE sidetone, never the
mic), USB voice (the mic) and a modem over sent like the RTTY bar's open
channel (the modem audio, not the mic the page keeps streaming). It needs
playwright-core and a Chromium and is skipped without them.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _node(*args, timeout=240):
    return subprocess.run(["node", str(ROOT / "tests" / "recorder_browser.mjs"), *args],
                          check=False, capture_output=True, text=True, timeout=timeout)


def test_recorder_browser():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    result = _node()
    assert result.returncode == 0, result.stdout + result.stderr
