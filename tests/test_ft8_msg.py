"""FT8 / FT4 with a nonstandard call (#153).

A message with two plain calls only packs when both are standard. The page used
to build "AO40EUDXF VA2BBW FN35" regardless, the codec fell back to 13 characters
of free text and "AO40EUDXF VA2" went out. tests/ft8_msg_sim.mjs takes what
digi-msg.js builds through the real ft8ts codec, every message of a QSO in both
modes; tests/ft8_msg_browser.mjs runs whole QSOs through the page's sequencer on
two Standalone virtual rigs, both ways round, and one between standard calls.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_ft8_msg_sim():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = subprocess.run(
        ["node", str(ROOT / "tests" / "ft8_msg_sim.mjs")],
        check=False,
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_digi_msg_wired_into_both_builds():
    """It has to load ahead of the page: the page's start-up builds the rows."""
    qrc = (ROOT / "resources" / "web.qrc").read_text(encoding="utf-8")
    static = (ROOT / "tools" / "build-static.sh").read_text(encoding="utf-8")
    assert 'alias="digi-msg.js">web-shared/digi-msg.js' in qrc
    assert "digi-msg.js" in static
    for fork in ("web", "web-standalone"):
        page = (ROOT / "resources" / fork / "index.html").read_text(encoding="utf-8")
        assert page.index('<script src="digi-msg.js">') < page.index("function getDigiBaseMsgTexts")


def test_ft8_msg_browser():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    script = str(ROOT / "tests" / "ft8_msg_browser.mjs")
    probe = subprocess.run(["node", script, "--probe"], check=False, capture_output=True, text=True, timeout=30)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium")
    result = subprocess.run(["node", script], check=False, capture_output=True, text=True, timeout=420)
    assert result.returncode == 0, result.stdout + result.stderr
