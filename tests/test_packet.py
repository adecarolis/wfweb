"""Packet (Dire Wolf) loopback self-test.

Runs `wfweb --packet-self-test`, which encodes a known AX.25 UI frame via
Dire Wolf's TX path, feeds the resulting PCM back through the demodulator
in-process, and exits 0 only if the decoded src/dst/info match the input.

Skipped when the binary was built without PACKET_SUPPORT (the flag is
absent from --help) or the binary isn't present.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

WFWEB_BIN = Path(__file__).resolve().parent.parent / "wfweb"


def _has_packet_support() -> bool:
    if not WFWEB_BIN.exists():
        return False
    out = subprocess.run(
        [str(WFWEB_BIN), "--help"],
        capture_output=True, text=True, timeout=5,
    ).stdout
    return "--packet-self-test" in out


pytestmark = pytest.mark.skipif(
    not _has_packet_support(),
    reason="wfweb not built with PACKET_SUPPORT (rebuild with qmake CONFIG+=packet)",
)


def test_packet_loopback_self_test():
    """Dire Wolf AX.25 encode -> demod loopback decodes src/dst/info correctly."""
    proc = subprocess.run(
        [str(WFWEB_BIN), "--packet-self-test"],
        capture_output=True, text=True, timeout=15,
    )
    assert proc.returncode == 0, (
        f"packet self-test failed (exit {proc.returncode}).\n"
        f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
    )
    # Belt-and-braces: confirm the expected success log line is present so
    # a future regression where the binary exits 0 without running the test
    # still fails loudly.
    combined = proc.stdout + proc.stderr
    assert "SelfTest: ALL MODES PASS" in combined, (
        f"'SelfTest: ALL MODES PASS' marker not found in output:\n{combined}"
    )
