"""Build and run the self-contained C++ unit tests (no rig, no server).

Each entry is a test program under tests/ plus the sources it needs.  They
link only against Qt5Core (plus the modules MODULES names, Qt5Network for
the access control's PBKDF2), so a plain g++ + pkg-config is enough.
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
    # Windows launcher helpers: wfweb command line composed from the form,
    # and the server log lines the launcher reacts to.
    "launcher": ["launcher/src/launchconfig.cpp", "launcher/src/logparser.cpp",
                 "launcher/src/portowner.cpp"],
    # Shared Station access control: password hashing, accounts, setup code,
    # lockout, sessions, API tokens, audit log.
    "access": ["src/access.cpp"],
}

# Qt modules beyond Qt5Core a unit needs.
MODULES = {
    "access": ["Qt5Network"],
}


@pytest.mark.parametrize("unit", sorted(UNITS))
def test_cpp_unit(unit, tmp_path):
    if not shutil.which("g++") or not shutil.which("pkg-config"):
        pytest.skip("C++ compiler/pkg-config unavailable")
    modules = ["Qt5Core", *MODULES.get(unit, [])]
    flags = subprocess.run(
        ["pkg-config", "--cflags", "--libs", *modules],
        check=False,
        capture_output=True,
        text=True,
    )
    if flags.returncode:
        pytest.skip(f"{' '.join(modules)} development package unavailable")

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
