"""Every WebSocket command the server handles is classified.

A command missing from the capability table in access.cpp is Admin-only
(fail closed), so one added to webServer::handleCommand and forgotten there
works for the admin who wrote it and is refused to every member:
setTsqlFreq was, and a member could not set the tone-squelch tone
(2026-11-17).  Source check, no server needed.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _handled():
    src = (ROOT / "src" / "webserver.cpp").read_text(errors="replace")
    start = src.index("void webServer::handleCommand(")
    body = src[start:src.index("\n}\n", start)]
    return set(re.findall(r'type == "([A-Za-z0-9_]+)"', body))


def _classified():
    src = (ROOT / "src" / "access.cpp").read_text(errors="replace")
    names = set()
    for table in ("kViewCommands", "kControlCommands", "kTxCommands", "kAdminCommands"):
        m = re.search(table + r"\[\]\s*=\s*\{(.*?)\};", src, re.S)
        assert m, f"{table} not found in access.cpp"
        body = re.sub(r"//[^\n]*", "", m.group(1))
        names |= set(re.findall(r'"([A-Za-z0-9_]+)"', body))
    return names


def test_every_handled_command_is_in_the_capability_table():
    handled = _handled()
    assert len(handled) > 60, "handleCommand not parsed"
    missing = sorted(handled - _classified())
    assert not missing, f"handled by webServer::handleCommand but not in access.cpp's tables (Admin-only by default): {missing}"
