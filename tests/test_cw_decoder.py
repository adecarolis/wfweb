"""CW decoder worker (resources/web-shared/cw-decoder-worker.js).

tests/cw_decoder_sim.mjs loads the real module worker under node, with the
vendored ONNX Runtime wasm and the DeepCW model it ships, and feeds it
synthesized CW in one-second ticks the way the page does: a clean 20 wpm
CQ, a 25 wpm QSO over at -6 dB and a 30 wpm CQ at -10 dB must come back
exact, committed word by word by label position on the sliding window, and
the last word committed by the trailing-silence rule.

The two-rig bench run (A keys through the virtual rig, B decodes in a real
Chromium) is not automated here; see the DeepCW notes in CLAUDE.md.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_cw_decoder_sim():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = subprocess.run(
        ["node", str(ROOT / "tests" / "cw_decoder_sim.mjs")],
        check=False,
        capture_output=True,
        text=True,
        timeout=600,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_decoder_assets_wired_into_both_builds():
    """The model, its metadata and the ORT runtime reach both builds."""
    qrc = (ROOT / "resources" / "web.qrc").read_text(encoding="utf-8")
    static = (ROOT / "tools" / "build-static.sh").read_text(encoding="utf-8")
    for name in ("models/deepcw.onnx", "models/deepcw.onnx.json",
                 "ort/ort.wasm.min.mjs", "ort/ort-wasm-simd-threaded.mjs",
                 "ort/ort-wasm-simd-threaded.wasm"):
        assert f'alias="{name}"' in qrc, name + " missing from web.qrc"
        assert (ROOT / "resources" / "web-shared" / name).exists(), name
    assert "deepcw.onnx.json" in static and '"$SHARED"/ort/*' in static
    # ggmorse stays in both builds as the fallback engine (and the comparison line while COMPARE_GGMORSE is on)
    assert 'alias="cw-decoder-worker-ggmorse.js"' in qrc and "cw-decoder-worker-ggmorse.js" in static


def _get(web_port, path, headers=None):
    import requests
    import urllib3

    urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    for scheme in ("https", "http"):
        try:
            r = requests.get(f"{scheme}://127.0.0.1:{web_port}{path}", headers=headers or {}, verify=False, timeout=10)
            return r
        except requests.RequestException:
            continue
    return None


def test_server_revalidates_model_and_runtime(wfweb_instance):
    """/models/ and /ort/ carry an ETag and answer 304 to If-None-Match, so a
    browser keeps the 27 MB and revalidates with one small round trip; every
    other resource stays no-store."""
    _, web_port = wfweb_instance
    r = _get(web_port, "/models/deepcw.onnx.json")
    assert r is not None and r.status_code == 200
    etag = r.headers.get("ETag")
    assert etag and etag.startswith('"'), r.headers
    assert r.headers.get("Cache-Control") == "no-cache", r.headers
    assert "no-store" not in r.headers.get("Cache-Control", "")
    r2 = _get(web_port, "/models/deepcw.onnx.json", {"If-None-Match": etag})
    assert r2.status_code == 304, (r2.status_code, r2.headers)
    assert r2.headers.get("ETag") == etag
    assert len(r2.content) == 0
    r3 = _get(web_port, "/ort/ort.wasm.min.mjs")
    assert r3.status_code == 200 and r3.headers.get("ETag")
    r4 = _get(web_port, "/cw-decoder.js")
    assert "no-store" in r4.headers.get("Cache-Control", "") and "ETag" not in r4.headers


def test_page_falls_back_to_ggmorse():
    """The page probes WASM SIMD before downloading anything, and keeps ggmorse
    as the engine where DeepCW cannot load or cannot keep up."""
    page = (ROOT / "resources" / "web-shared" / "cw-decoder.js").read_text(encoding="utf-8")
    for needle in ("function wasmSimdSupported()", "function fallbackToGgmorse(", "localStorage.getItem(ENGINE_KEY)",
                   "'cw-decoder-worker-ggmorse.js'"):
        assert needle in page, needle
    gg = (ROOT / "resources" / "web-shared" / "cw-decoder-worker-ggmorse.js").read_text(encoding="utf-8")
    assert "upsample3200to4000" in gg, "the ggmorse worker must take the page's 3200 Hz tap"


def test_page_starts_a_module_worker():
    page = (ROOT / "resources" / "web-shared" / "cw-decoder.js").read_text(encoding="utf-8")
    assert "{ type: 'module' }" in page, "cw-decoder-worker.js is an ES module and needs a module worker"
    assert "const SAMPLE_RATE = 3200;" in page, "the DeepCW model expects 3200 Hz input"
