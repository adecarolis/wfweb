"""Packet (Dire Wolf) loopback self-test.

Runs `wfweb --packet-self-test`, which encodes a known AX.25 UI frame via
Dire Wolf's TX path, feeds the resulting PCM back through the demodulator
in-process, and exits 0 only if the decoded src/dst/info match the input
for every supported baud (300 / 1200 / 9600).

The self-test also exercises the dlq init + drain path that used to abort
with `Assertion 'was_init' failed` when the Dire Wolf connected-mode
source was vendored in without a matching init call on the standalone
self-test path — the checks below guard that regression explicitly.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

WFWEB_BIN = Path(__file__).resolve().parent.parent / "wfweb"

SELF_TEST_MODES = (300, 1200, 9600)

# Substrings that would indicate the dlq init/drain regression reappeared.
# `Assertion ... was_init` is the original libc abort message; `Aborted`
# surfaces in the shell message after SIGABRT; `append_to_queue` is the
# function name in dlq.c where the assertion fires.
REGRESSION_MARKERS = (
    "Assertion",
    "was_init",
    "append_to_queue",
    "Aborted",
    "core dumped",
)


pytestmark = pytest.mark.skipif(
    not WFWEB_BIN.exists(),
    reason=f"wfweb binary not built at {WFWEB_BIN} — run `qmake wfweb.pro && make` first",
)


@pytest.fixture(scope="module")
def self_test_result() -> subprocess.CompletedProcess[str]:
    """Run `wfweb --packet-self-test` once per module and cache the result."""
    return subprocess.run(
        [str(WFWEB_BIN), "--packet-self-test"],
        capture_output=True, text=True, timeout=15,
    )


def test_packet_loopback_self_test(self_test_result):
    """Dire Wolf AX.25 encode -> demod loopback decodes src/dst/info correctly."""
    proc = self_test_result
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


@pytest.mark.parametrize("baud", SELF_TEST_MODES)
def test_packet_self_test_per_mode_pass_marker(self_test_result, baud):
    """Each supported baud rate must emit its per-mode PASS marker.

    Catches a regression where one specific mode's encode/decode round-trip
    breaks while the others still work — `ALL MODES PASS` would never be
    printed in that case, but this parametrised test reports *which* mode
    regressed instead of masking it behind a single failure.
    """
    combined = self_test_result.stdout + self_test_result.stderr
    marker = f"SelfTest[ {baud} ]: PASS"
    assert marker in combined, (
        f"per-mode marker {marker!r} missing from self-test output:\n{combined}"
    )


def test_packet_self_test_does_not_abort(self_test_result):
    """Guard against the dlq was_init regression explicitly.

    Before the fix, RX path decoding tripped `assert(was_init)` in
    resources/direwolf/src/dlq.c:append_to_queue because the standalone
    self-test never called dlq_init().  The process aborted (SIGABRT)
    after the first successful demod, so `--packet-self-test` exited 134
    and left an `Assertion ... was_init' failed.` line on stderr.
    """
    proc = self_test_result
    combined = proc.stdout + proc.stderr
    for needle in REGRESSION_MARKERS:
        assert needle not in combined, (
            f"regression marker {needle!r} found in self-test output — "
            f"the dlq init/drain fix may have regressed.\n"
            f"exit={proc.returncode}\nstdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    # Also assert a clean abort-free exit code.  134 = 128+SIGABRT.
    assert proc.returncode != 134, (
        "self-test exited with SIGABRT (134) — see regression markers above"
    )


def test_packet_self_test_idempotent_dlq_init():
    """Running the self-test twice in one process (across 3 modes each) must
    not double-init dlq.

    `runSelfTest()` constructs a fresh DireWolfProcessor for every baud and
    calls init() on each — three times per run.  If
    DireWolfProcessor::ensureDlqInitialized() lost its std::call_once guard,
    the second call into dlq_init() would re-run pthread_mutex_init on an
    already-initialised mutex and either crash or deadlock later.  The
    self-test completing exit-0 is itself evidence the guard holds; this
    test codifies that expectation so a silent regression (e.g. someone
    reverting to plain dlq_init()) is caught.

    We also run the whole binary twice back-to-back as a belt-and-braces
    check on process-startup paths.
    """
    for attempt in (1, 2):
        proc = subprocess.run(
            [str(WFWEB_BIN), "--packet-self-test"],
            capture_output=True, text=True, timeout=15,
        )
        assert proc.returncode == 0, (
            f"self-test attempt {attempt} failed (exit {proc.returncode}).\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
        combined = proc.stdout + proc.stderr
        assert "SelfTest: ALL MODES PASS" in combined, (
            f"attempt {attempt}: 'SelfTest: ALL MODES PASS' missing:\n{combined}"
        )
