"""The Server build's start screen, in a browser, on the IC-7300 mock.

The start screen is a translucent sheet over the live page, there for the
user gesture a browser needs before it plays audio. A click anywhere on it,
a tap or a key starts the page, and none of them may reach the radio under
it: with the old opaque splash Space keyed the rig and the arrows tuned.
tests/start_screen_browser.mjs has the list.

Needs node, playwright-core and a Chromium; skipped without them.
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _node(*args, timeout=300, env=None):
    return subprocess.run(["node", str(ROOT / "tests" / "start_screen_browser.mjs"), *args],
                          check=False, capture_output=True, text=True, timeout=timeout,
                          env=None if env is None else {**os.environ, **env})


def test_start_screen(wfweb_7300_instance):
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    _, web_port = wfweb_7300_instance
    result = _node(env={"WFWEB_START_URL": f"https://127.0.0.1:{web_port}/"})
    assert result.returncode == 0, result.stdout + result.stderr
