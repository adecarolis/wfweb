"""CW keyer model, pacing and browser sidetone (resources/web-shared/cw-keyer.js).

tests/cw_keyer_sim.mjs drives the module under node with a virtual clock, a
recording rig and a fake AudioContext: PARIS timing, seven-dot word gaps,
pacing that never overflows nor starves the rig's keyer buffer, Backspace
rewinding the clock, characters the keyer cannot send, and a sidetone that
keeps every element across spaces and pauses (#141).

tests/cw_paddle_sim.mjs drives cw-paddle.js the same way: a MIDI paddle or
straight key as CW text entry, decoded in the browser and fed to the keyer.

tests/cw_bench.mjs is the end-to-end check on the two-rig virtual bench: the
page's model against the radio's actual keyed audio, CHR-mode type-ahead
against the virtual rig's keyer buffer, Backspace, STOP and the sidetone in a
real browser.  It needs the bench binaries, node, playwright-core and a
Chromium (see the script's findBrowser) and is skipped without them.

tests/cw_rig_capture.mjs (not run here: it keys the real IC-7300 with BK-IN
OFF) measures the radio's keyer against the model per speed; the Icom dot
table in cw-keyer.js and the virtual rig came from it.
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_cw_keyer_sim():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = subprocess.run(
        ["node", str(ROOT / "tests" / "cw_keyer_sim.mjs")],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_cw_paddle_sim():
    """Paddle / straight key as text entry (cw-paddle.js): synthetic contact
    timelines decode to their text in iambic A and B, swapped, at several
    speeds; a jittery straight key decodes from another speed's estimate; the
    sidetone follows the paddle and the text path adds no second tone."""
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = subprocess.run(
        ["node", str(ROOT / "tests" / "cw_paddle_sim.mjs")],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_paddle_input_sim():
    """USB keyboard paddle adapter (VBand: left / right Ctrl) and the paddle
    settings (paddle-panel.js): off by default, repeats ignored, keys held
    back mid-character, focus loss, straight key, LEARN, a keyed CQ."""
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = subprocess.run(
        ["node", str(ROOT / "tests" / "paddle_input_sim.mjs")],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_both_pages_load_cw_keyer():
    """Both forks load the shared module and no longer carry the model inline."""
    for fork in ("web", "web-standalone"):
        page = (ROOT / "resources" / fork / "index.html").read_text(encoding="utf-8")
        assert '<script src="cw-keyer.js"></script>' in page, fork
        assert "new WfCwKeyer(" in page, fork
        assert "var CW_MORSE" not in page, fork + " still defines the Morse table inline"


def test_server_serves_cw_keyer(wfweb_instance):
    """The qrc alias serves cw-keyer.js next to the page."""
    import requests
    import urllib3

    urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    _, web_port = wfweb_instance
    body = None
    for scheme in ("https", "http"):
        try:
            r = requests.get(f"{scheme}://127.0.0.1:{web_port}/cw-keyer.js", verify=False, timeout=5)
            if r.status_code == 200:
                body = r.text
                break
        except requests.RequestException:
            continue
    assert body is not None, "cw-keyer.js not served"
    assert "WfCwKeyer" in body and "WfCwSidetone" in body


def test_cw_bench():
    if os.environ.get("WFWEB_SKIP_BENCH"):
        pytest.skip("WFWEB_SKIP_BENCH set")
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    for binary in (ROOT / "wfweb", ROOT / "tools" / "virtualrig" / "virtualrig"):
        if not binary.exists():
            pytest.skip(f"{binary.name} not built")
    probe = subprocess.run(["node", str(ROOT / "tests" / "cw_bench.mjs"), "--probe"], capture_output=True, text=True)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium for the bench: " + probe.stdout.strip())
    testrig = ROOT / "scripts" / "testrig.sh"
    subprocess.run([str(testrig), "down"], capture_output=True, text=True, timeout=60)
    up = subprocess.run([str(testrig), "up", "2"], capture_output=True, text=True, timeout=120)
    assert up.returncode == 0, up.stdout + up.stderr
    try:
        result = subprocess.run(
            ["node", str(ROOT / "tests" / "cw_bench.mjs")],
            check=False,
            capture_output=True,
            text=True,
            timeout=480,
        )
    finally:
        subprocess.run([str(testrig), "down"], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
