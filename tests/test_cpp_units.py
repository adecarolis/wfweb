"""Build and run the self-contained C++ unit tests (no rig, no server).

Each entry is a test program under tests/ plus the sources it needs.  They
link only against Qt5Core, so a plain g++ + pkg-config is enough.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]

UNITS = {
    # WSJT-X NetworkMessage serialization, checked by deserializing with Qt's
    # own schema-3 representation.
    "wsjtx_protocol": ["src/wsjtxmessage.cpp"],
    # ADIF parse/append/rewrite, chronological index, paging, merge.
    "logbook": ["src/logbook.cpp"],
    # Terminal status page layout at every terminal size.
    "consolestatus": ["src/consolestatus.cpp"],
}


@pytest.mark.parametrize("unit", sorted(UNITS))
def test_cpp_unit(unit, tmp_path):
    if not shutil.which("g++") or not shutil.which("pkg-config"):
        pytest.skip("C++ compiler/pkg-config unavailable")
    flags = subprocess.run(
        ["pkg-config", "--cflags", "--libs", "Qt5Core"],
        check=False,
        capture_output=True,
        text=True,
    )
    if flags.returncode:
        pytest.skip("Qt5Core development package unavailable")

    executable = tmp_path / f"{unit}_test"
    command = [
        "g++", "-std=c++17", "-fPIC",
        "-I", str(ROOT / "include"),
        str(ROOT / "tests" / f"{unit}_test.cpp"),
        *(str(ROOT / src) for src in UNITS[unit]),
        *flags.stdout.split(),
        "-o", str(executable),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    subprocess.run([str(executable)], check=True, cwd=ROOT)
