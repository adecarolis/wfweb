"""Web MIDI controller support (resources/web-shared/midi.js).

tests/midi_sim.mjs drives WfMidi and WfMidiTuner under node with a fake
MIDIAccess: the CTR2-MIDI presets (knob encodings, buttons, paddle jack, the
Extended BTN layout), MIDI learn and persistence, hot-plug with held gates
released on unplug, a browser without Web MIDI, and knob-tick coalescing.

tests/midi_browser.mjs is the in-browser check: the Standalone bundle on its
in-browser virtual rig with a fake navigator.requestMIDIAccess, driving the
VFO, a band key, AF and the paddle -> CW transcript.  It needs playwright-core
and a Chromium (found like cw_bench.mjs) and is skipped without them.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def _node(script, *args, timeout=300):
    return subprocess.run(
        ["node", str(ROOT / "tests" / script), *args],
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def test_midi_sim():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = _node("midi_sim.mjs")
    assert result.returncode == 0, result.stdout + result.stderr


def test_both_pages_load_midi():
    """Both forks load the shared modules and register the controller."""
    for fork in ("web", "web-standalone"):
        page = (ROOT / "resources" / fork / "index.html").read_text(encoding="utf-8")
        assert '<script src="cw-paddle.js"></script>' in page, fork
        assert '<script src="midi.js"></script>' in page, fork
        assert '<script src="paddle-panel.js"></script>' in page, fork
        assert 'id="paddleOverlay"' in page and 'id="cwPaddleBtn"' in page, fork
        assert "new WfMidi(" in page and "new WfCwPaddle(" in page, fork
        assert 'id="midiOverlay"' in page, fork


def test_build_plumbing():
    """Server qrc aliases and the Standalone copy list carry both modules."""
    qrc = (ROOT / "resources" / "web.qrc").read_text(encoding="utf-8")
    build = (ROOT / "tools" / "build-static.sh").read_text(encoding="utf-8")
    for name in ("midi.js", "cw-paddle.js", "paddle-panel.js"):
        assert f'<file alias="{name}">web-shared/{name}</file>' in qrc, name
        assert name in build, name


def test_server_serves_midi(wfweb_instance):
    import requests
    import urllib3

    urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    _, web_port = wfweb_instance
    bodies = {}
    for name in ("midi.js", "cw-paddle.js"):
        for scheme in ("https", "http"):
            try:
                r = requests.get(f"{scheme}://127.0.0.1:{web_port}/{name}", verify=False, timeout=5)
                if r.status_code == 200:
                    bodies[name] = r.text
                    break
            except requests.RequestException:
                continue
    assert "WfMidi" in bodies.get("midi.js", ""), "midi.js not served"
    assert "WfCwPaddle" in bodies.get("cw-paddle.js", ""), "cw-paddle.js not served"


def test_midi_browser():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("midi_browser.mjs", "--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    result = _node("midi_browser.mjs")
    assert result.returncode == 0, result.stdout + result.stderr
