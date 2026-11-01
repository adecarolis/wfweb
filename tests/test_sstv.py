"""SSTV codec: the `sstv` Rust crate (resources/sstv-shim/) as Asyncified
WebAssembly behind resources/web-shared/sstv-worker.js.

tests/sstv_codec_sim.mjs loads the real module worker under node with the
sstv.wasm both builds ship, has it encode colour bars in several modes (the
TX path at 12 kHz), resamples them to the rig's rate, adds calibrated white
noise and feeds them back in 20 ms Int16 chunks the way handleAudioData
does.  Clean images in Robot 36, Martin 1, Scottie 1 and PD-120 must decode
completely with the mode read from the VIS header; 8 kHz, a 0.1 % clock
error, a gap plus two images back to back, a reset mid-image, a pinned mode
without a header and +12 dB SNR (in 2.5 kHz, through the worker's quadrature
front end) are checked; the raw path and +9 / +6 dB are measured and printed.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def _node(script, *args, timeout=300):
    return subprocess.run(["node", str(ROOT / "tests" / script), *args],
                          check=False, capture_output=True, text=True, timeout=timeout)


def test_sstv_codec_sim():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    result = subprocess.run(
        ["node", str(ROOT / "tests" / "sstv_codec_sim.mjs")],
        check=False,
        capture_output=True,
        text=True,
        timeout=600,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_sstv_wasm_is_asyncified_and_small():
    """The committed module carries the Asyncify exports the glue needs, the
    three host imports and nothing else, and stays small (no SIMD, no std
    bloat): 71 KB at v0.3.1."""
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    wasm = ROOT / "resources" / "web-standalone" / "wasm" / "sstv.wasm"
    assert wasm.exists(), "run tools/build-sstv-wasm.sh"
    assert wasm.stat().st_size < 400_000, "sstv.wasm grew past 400 KB; check the build flags"
    script = """
const fs = require('fs');
const m = new WebAssembly.Module(fs.readFileSync(process.argv[1]));
const ex = WebAssembly.Module.exports(m).map(e => e.name);
const im = WebAssembly.Module.imports(m).map(i => i.module + '.' + i.name);
console.log(JSON.stringify({ex, im}));
"""
    out = subprocess.run(["node", "-e", script, str(wasm)], check=True, capture_output=True, text=True, timeout=60).stdout
    import json
    info = json.loads(out)
    for name in ("sstv_alloc", "sstv_free", "sstv_mode_count", "sstv_mode_name", "sstv_mode_samples", "sstv_mode_header_samples",
                 "sstv_mode_vis", "sstv_mode_layout",
                 "sstv_rx_push", "sstv_rx_reset", "sstv_rx_decode", "sstv_tx_begin", "sstv_tx_fill", "sstv_tx_end",
                 "asyncify_start_unwind", "asyncify_stop_unwind", "asyncify_start_rewind", "asyncify_stop_rewind",
                 "asyncify_get_state", "memory"):
        assert name in info["ex"], name
    assert sorted(info["im"]) == ["env.sstv_await_samples", "env.sstv_on_mode", "env.sstv_on_row"], info["im"]


def test_sstv_codec_assets_wired_into_both_builds():
    """Worker, codec loader, Asyncify glue and the wasm reach both builds."""
    qrc = (ROOT / "resources" / "web.qrc").read_text(encoding="utf-8")
    static = (ROOT / "tools" / "build-static.sh").read_text(encoding="utf-8")
    shared = ROOT / "resources" / "web-shared"
    standalone = ROOT / "resources" / "web-standalone"
    assert (shared / "sstv-worker.js").exists()
    assert 'alias="sstv-worker.js"' in qrc
    assert "sstv-worker.js" in static, "sstv-worker.js missing from build-static.sh's web-shared list"
    for name in ("sstv.wasm", "sstv-codec.mjs", "asyncify.mjs"):
        assert (standalone / "wasm" / name).exists(), name
        assert f'alias="wasm/{name}"' in qrc, name + " missing from web.qrc"
    # build-static.sh copies web-standalone/wasm/*.mjs and *.wasm wholesale.
    assert "*.mjs" in static and "*.wasm" in static
    worker = (shared / "sstv-worker.js").read_text(encoding="utf-8")
    assert "new URL('./wasm/sstv-codec.mjs', import.meta.url)" in worker, "the fingerprinter rewrites this literal"
    codec = (standalone / "wasm" / "sstv-codec.mjs").read_text(encoding="utf-8")
    assert "new URL('./sstv.wasm', import.meta.url)" in codec
    assert "from './asyncify.mjs'" in codec
    shim = ROOT / "resources" / "sstv-shim"
    assert (shim / "Cargo.lock").exists(), "commit Cargo.lock with the wasm"
    # The crate is vendored with the re-lock patch; the patch and the copy
    # must agree (README-vendoring.md: re-apply it on an upstream bump).
    assert 'path = "vendor/sstv"' in (shim / "Cargo.toml").read_text(encoding="utf-8")
    vendored = shim / "vendor" / "sstv"
    assert (vendored / "LICENSE").exists() and (vendored / "build.rs").exists()
    assert "9761c4c61a6353d7ae2173e1579e434e6c1b1ce3" in (vendored / "Cargo.toml").read_text(encoding="utf-8")
    cutter = (vendored / "src" / "decode" / "assembler" / "cutter.rs").read_text(encoding="utf-8")
    assert "pub fn relock(" in cutter and "relock: Option<SyncLock>" in cutter, "sstv-relock.patch not applied to the vendored crate"
    assert "pub fn relock(" in (shim / "sstv-relock.patch").read_text(encoding="utf-8")
    # ... and the layout patch: the worker's own image search reads each
    # mode's sequence (sync spacing, control tones) through the shim.
    mode_rs = (shim / "vendor" / "sstv" / "src" / "modes" / "mode.rs").read_text(encoding="utf-8")
    assert "pub fn control_tones(" in mode_rs and "pub fn sync_spacing(" in mode_rs, "sstv-layout-api.patch not applied to the vendored crate"
    assert "pub fn control_tones(" in (shim / "sstv-layout-api.patch").read_text(encoding="utf-8")
    worker = (ROOT / "resources" / "web-shared" / "sstv-worker.js").read_text(encoding="utf-8")
    assert "modeLayout(" in worker and "modeVis(" in worker and "case 'start':" in worker


def test_sstv_browser():
    """Two Standalone virtual rigs in a real browser: A sends a Robot 36
    picture from the panel, B decodes it, files it in its gallery and deletes
    it; the over is keyed as a digital one and the mic level goes back on
    close (tests/sstv_browser.mjs)."""
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("sstv_browser.mjs", "--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    result = _node("sstv_browser.mjs", timeout=400)
    assert result.returncode == 0, result.stdout + result.stderr
