"""RTTY decoder worker (resources/web-shared/rtty-decoder-worker.js).

tests/rtty_decoder_sim.mjs loads the real module worker under node, with the
vendored ONNX Runtime wasm and the DeepRTTY model it ships, and feeds it
AFSK made by the page's own synthesizer (rtty.js) at the rig's rate with the
tones where a rig puts them (2125 / 2295 Hz, mark low on LSB, mark high on
USB), in 100 ms Int16 chunks the way handleAudioData does.  A clean CQ and
a QSO over at -6 dB must come back exact, committed once by position on the
sliding window; -10 dB stays under 15 % character errors; a figures run
keeps its shift across ticks; the classic FSK engine copies a clean over.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_rtty_decoder_sim():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = subprocess.run(
        ["node", str(ROOT / "tests" / "rtty_decoder_sim.mjs")],
        check=False,
        capture_output=True,
        text=True,
        timeout=600,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_rtty_assets_wired_into_both_builds():
    """The bar, the DSP module, the worker and the model reach both builds."""
    qrc = (ROOT / "resources" / "web.qrc").read_text(encoding="utf-8")
    static = (ROOT / "tools" / "build-static.sh").read_text(encoding="utf-8")
    for name in ("rtty.js", "rtty-panel.js", "rtty-panel.css", "rtty-decoder-worker.js",
                 "models/deeprtty.onnx", "models/deeprtty.onnx.json", "models/deeprtty-LICENSE"):
        assert f'alias="{name}"' in qrc, name + " missing from web.qrc"
        assert (ROOT / "resources" / "web-shared" / name).exists(), name
        assert name.split("/")[-1] in static, name + " missing from build-static.sh"
    for fork in ("web", "web-standalone"):
        page = (ROOT / "resources" / fork / "index.html").read_text(encoding="utf-8")
        for needle in ('<script src="rtty.js">', '<script src="rtty-panel.js">', 'href="rtty-panel.css"',
                       "RttyPanel.onModeChange(mode, prevMode)", "RttyPanel.feedRx(pcmData)",
                       "RttyPanel.hide({ restore: false })", "RttyPanel.resume()", "RttyPanel.suspend()",
                       "return 'RTTY';", ".rtty-bar:not(.hidden)"):
            assert needle in page, f"{fork}: {needle}"


def test_worker_is_a_module_fed_with_the_rigs_pcm():
    panel = (ROOT / "resources" / "web-shared" / "rtty-panel.js").read_text(encoding="utf-8")
    assert "{ type: 'module' }" in panel, "rtty-decoder-worker.js is an ES module and needs a module worker"
    assert "type: 'configure', inputRate: S.inputRate" in panel and "S.inputRate = host('audioSampleRate')" in panel, "the worker translates from the page's audio rate"
    assert "function wasmSimdSupported()" in panel and "function fallbackToClassic(" in panel
    worker = (ROOT / "resources" / "web-shared" / "rtty-decoder-worker.js").read_text(encoding="utf-8")
    assert "from './rtty.js'" in worker, "the fingerprinter versions `from` imports only"
    assert "noise_level_rayleigh_factor" in worker, "the DeepRTTY normalisation (noise_q25_active_log1p)"
    assert "wfweb-rtty-model" in worker, "model + runtime kept in Cache Storage with their ETag"


def _get(web_port, path, headers=None):
    import requests
    import urllib3

    urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    for scheme in ("https", "http"):
        try:
            return requests.get(f"{scheme}://127.0.0.1:{web_port}{path}", headers=headers or {}, verify=False, timeout=10)
        except requests.RequestException:
            continue
    return None


def test_server_serves_the_model_revalidable(wfweb_instance):
    """/models/deeprtty.* carry an ETag and answer 304, like the DeepCW files."""
    _, web_port = wfweb_instance
    r = _get(web_port, "/models/deeprtty.onnx.json")
    assert r is not None and r.status_code == 200, r
    etag = r.headers.get("ETag")
    assert etag and r.headers.get("Cache-Control") == "no-cache", r.headers
    assert r.json()["rtty_baud"] == 45.45
    r2 = _get(web_port, "/models/deeprtty.onnx.json", {"If-None-Match": etag})
    assert r2.status_code == 304
    r3 = _get(web_port, "/rtty-panel.js")
    assert r3.status_code == 200 and "no-store" in r3.headers.get("Cache-Control", "")
    r4 = _get(web_port, "/rtty-decoder-worker.js")
    assert r4.status_code == 200 and "import * as rttyModule from './rtty.js'" in r4.text
