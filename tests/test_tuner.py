"""The tuner controls (#156): the top-bar TUNE tag stays on every rig with a
tuner, grey OFF / red ON / blinking while it tunes; a tap turns the tuner on
or off and a hold tunes, on the tag and on FUNC > TUNER alike.

tests/tuner_browser.mjs is the in-browser check: the Standalone bundle on the
virtual rig (mouse, keyboard and touch), and the Web Serial transport against
a fake IC-7300 that keeps the tuner per band, as the radio does with
[TUNER] Switch = Auto. It needs playwright-core and a Chromium and is skipped
without them.
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _node(script, *args, timeout=300):
    return subprocess.run(["node", str(ROOT / "tests" / script), *args],
                          check=False, capture_output=True, text=True, timeout=timeout,
                          env=dict(os.environ))


def test_tuner_browser():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("tuner_browser.mjs", "--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    result = _node("tuner_browser.mjs")
    assert result.returncode == 0, result.stdout + result.stderr
