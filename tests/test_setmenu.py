"""SET menu window (resources/web-shared/setmenu.js).

tests/setmenu_sim.mjs drives WfSetMenu under node against a fake radio: the
codec of each item type, reads, and the checked write (read, set, read back;
nothing written when the read before it fails or no longer matches), and
validates every table in resources/web-shared/setmenu/.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_setmenu_sim():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    r = subprocess.run(["node", str(ROOT / "tests" / "setmenu_sim.mjs")],
                       capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stdout + r.stderr


def test_tables_are_served(rest_url):
    """The page fetches setmenu/<model>.json and setmenu.js from the server."""
    import requests
    base = rest_url.split("/api/")[0]
    # The plain REST port serves no SPA files; the web port does (HTTPS).
    web = base.replace("http://", "https://").rsplit(":", 1)
    port = int(web[1]) - 1
    for path in ("setmenu.js", "setmenu/IC-7300.json"):
        r = requests.get(f"https://127.0.0.1:{port}/{path}", verify=False, timeout=5)
        assert r.status_code == 200, (path, r.status_code)
    assert r.json()["model"] == "IC-7300"
