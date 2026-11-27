"""SET menu backup and restore: GET /api/v1/radio/setmenu/backup,
POST /api/v1/radio/setmenu/restore, WebSocket setMenuBackup / setMenuRestore.

Runs against the IC-7300 LAN mock (CI-V 0x94), with every item of the
IC-7300 table loaded into the mock's 1A 05 registers, as a radio would hold
them, and 0028 (TX Delay 70M) answering NG as on a radio without 70 MHz.
"""

import json
import threading
import time
from pathlib import Path

import pytest
import requests

ROOT = Path(__file__).resolve().parents[1]
TABLE = json.loads((ROOT / "resources/web-shared/setmenu/IC-7300.json").read_text(encoding="utf-8"))
ITEMS = {it["item"]: it for g in TABLE["groups"] for it in g["items"]}
NG_ITEM = "0028"
CTRL = 0xE2

RAW = {"hpflpf": "0025", "rgb": "017201910191", "date": "20261127", "time": "1052",
       "utcOffset": "050000", "ascii": "4142434445202020202020", "edges": "000007003007",
       "offset": "00100001"}


def valid_data(it):
    t = it["type"]
    if t in ("bool", "enum"):
        return (it.get("options") or [["00", "OFF"]])[0][0]
    if t in ("level", "number"):
        d = it.get("digits", 4)
        return str(max(it.get("min", 0), min(it.get("max", 128), 128))).zfill(d)
    return RAW.get(it.get("format"), "0102")


def reg(no):
    return int(no, 16)


def load_radio(mock):
    for no, it in ITEMS.items():
        mock.settings[reg(no)] = bytes.fromhex(valid_data(it))
    mock.settings.pop(reg(NG_ITEM), None)


@pytest.fixture
def radio(mock_radio_7300, monkeypatch):
    """The 7300 mock with a full SET menu; 0028 answers NG to E2."""
    mock = mock_radio_7300
    load_radio(mock)
    orig = mock.dispatch_civ

    def dispatch(cmd, payload, from_addr):
        if from_addr == CTRL and cmd == 0x1A and payload[:3] == bytes([0x05, 0x00, 0x28]):
            return mock.civ_nak(from_addr, 0x94)
        return orig(cmd, payload, from_addr)

    monkeypatch.setattr(mock, "dispatch_civ", dispatch)
    return mock


@pytest.fixture
def url(wfweb_7300_instance):
    _, web_port = wfweb_7300_instance
    return f"http://127.0.0.1:{web_port + 1}/api/v1/radio/setmenu"


def backup(url):
    r = requests.get(f"{url}/backup", timeout=120)
    assert r.status_code == 200, r.text
    return r.json()


def restore(url, b, dry=False):
    r = requests.post(f"{url}/restore" + ("?dryRun=1" if dry else ""), json=b, timeout=120)
    assert r.status_code == 200, r.text
    return r.json()


def sets_to(mock, no, since):
    """Set frames (1A 05 item data) from E2 to this item in the mock's log since index."""
    b = bytes.fromhex("1A05" + no)
    return [f for f in mock.civ_log[since:] if len(f) > 9 and f[3] == CTRL and f[4:8] == b and len(f) > 9]


def test_backup_reads_every_item(url, radio):
    b = backup(url)
    assert b["format"] == "wfweb-setmenu-backup" and b["version"] == 1 and b["model"] == "IC-7300"
    assert b["notOnRadio"] == [NG_ITEM]
    assert b["errors"] == {}
    assert len(b["items"]) == len(ITEMS) - 1
    for no, it in ITEMS.items():
        if no == NG_ITEM:
            continue
        assert b["items"][no]["data"] == valid_data(it), no
        assert b["items"][no]["name"] == it["name"]
    assert b["items"]["0029"]["value"] == "OFF"


def test_dry_run_lists_changes_and_writes_nothing(url, radio):
    b = backup(url)
    radio.settings[reg("0081")] = bytes.fromhex("0031")   # LCD Backlight changed on the radio
    radio.settings[reg("0029")] = bytes.fromhex("05")     # Time-Out Timer 30 min
    mark = len(radio.civ_log)
    r = restore(url, b, dry=True)
    assert r["dryRun"] is True
    changes = {c["item"]: c for c in r["changes"]}
    assert set(changes) == {"0081", "0029"}, r["changes"]
    assert changes["0029"]["from"] == "05" and changes["0029"]["to"] == "00"
    assert changes["0029"]["fromValue"] == "30 min" and changes["0029"]["toValue"] == "OFF"
    assert changes["0081"]["path"] == "SET > Display"
    assert radio.settings[reg("0081")] == bytes.fromhex("0031")
    assert not any(sets_to(radio, no, mark) for no in ITEMS), "a dry run sent a set"


def test_restore_writes_what_differs_and_checks_it(url, radio):
    b = backup(url)
    radio.settings[reg("0081")] = bytes.fromhex("0031")
    radio.settings[reg("0029")] = bytes.fromhex("05")
    mark = len(radio.civ_log)
    r = restore(url, b)
    assert r["dryRun"] is False and r["failed"] == []
    assert {c["item"] for c in r["written"]} == {"0081", "0029"}
    assert radio.settings[reg("0081")] == bytes.fromhex("0128")
    assert radio.settings[reg("0029")] == bytes.fromhex("00")
    # Only the two that differed were set, nothing else.
    assert sorted(no for no in ITEMS if sets_to(radio, no, mark)) == ["0029", "0081"]
    # A second restore finds nothing to do.
    r2 = restore(url, b)
    assert r2["written"] == [] and r2["failed"] == []


def test_read_only_and_clock_are_never_restored(url, radio):
    b = backup(url)
    radio.settings[reg("0094")] = bytes.fromhex("20270101")   # the clock moved on
    radio.settings[reg("0058")] = bytes.fromhex("0070")       # REF Adjust (calibration)
    radio.settings[reg("0076")] = bytes.fromhex("01")         # USB serial function: read-only here
    mark = len(radio.civ_log)
    r = restore(url, b)
    skipped = {s["item"]: s["reason"] for s in r["skipped"]}
    for no in ("0094", "0095", "0058", "0071", "0074", "0075", "0076", "0078", "0079", "0080"):
        assert no in skipped, no
    assert not sets_to(radio, "0094", mark) and not sets_to(radio, "0058", mark) and not sets_to(radio, "0076", mark)
    assert radio.settings[reg("0094")] == bytes.fromhex("20270101")
    assert r["written"] == []


def test_bad_values_never_reach_the_radio(url, radio):
    b = backup(url)
    b["items"]["0029"]["data"] = "09"            # not one of its codes
    b["items"]["0021"]["data"] = "0300"          # level out of range
    b["items"]["0104"]["data"] = "0172"          # raw of the wrong length
    b["items"]["9999"] = {"name": "nothing", "data": "00"}
    radio.settings[reg("0029")] = bytes.fromhex("05")
    mark = len(radio.civ_log)
    r = restore(url, b)
    failed = {f["item"]: f["reason"] for f in r["failed"]}
    assert set(failed) == {"0029", "0021", "0104"}, r["failed"]
    assert "refused" in failed["0029"]
    assert any(s["item"] == "9999" for s in r["skipped"])
    assert not sets_to(radio, "0029", mark) and not sets_to(radio, "0021", mark) and not sets_to(radio, "0104", mark)


def test_refusals(url, radio):
    b = backup(url)
    other = dict(b, model="IC-705")
    r = requests.post(f"{url}/restore", json=other, timeout=30)
    assert r.status_code == 400 and "IC-705" in r.json()["error"]
    r = requests.post(f"{url}/restore", json={"hello": 1}, timeout=30)
    assert r.status_code == 400
    r = requests.post(f"{url}/restore", data="nope", timeout=30)
    assert r.status_code == 400
    assert requests.get(f"{url}/restore", timeout=30).status_code == 405
    assert requests.post(f"{url}/backup", timeout=30).status_code == 405
    assert requests.get(f"{url}/elsewhere", timeout=30).status_code == 404
    # One run at a time: slow the mock's replies so the first backup is
    # still running (216 items at 10 ms) when the second asks.
    orig = radio.dispatch_civ

    def slow(cmd, payload, from_addr):
        if from_addr == CTRL:
            time.sleep(0.01)
        return orig(cmd, payload, from_addr)

    radio.dispatch_civ = slow
    results = {}
    t = threading.Thread(target=lambda: results.setdefault("first", requests.get(f"{url}/backup", timeout=120)))
    t.start()
    time.sleep(0.3)
    second = requests.get(f"{url}/backup", timeout=120)
    t.join()
    assert results["first"].status_code == 200
    assert second.status_code == 409, second.text


def test_websocket_progress_and_result(wfweb_7300_instance, radio):
    from conftest import ws_connect
    proc, web_port = wfweb_7300_instance
    ws = ws_connect(web_port, proc)
    try:
        ws.send(json.dumps({"cmd": "setMenuBackup", "id": 5}))
        progress, result = [], None
        deadline = time.monotonic() + 120
        while result is None:
            assert time.monotonic() < deadline
            msg = ws.recv(timeout=30)
            if isinstance(msg, bytes):
                continue
            m = json.loads(msg)
            if m.get("type") == "setMenuProgress" and m.get("id") == 5:
                progress.append(m["done"])
            elif m.get("type") == "setMenuResult" and m.get("id") == 5:
                result = m
        assert result["restore"] is False and len(result["items"]) == len(ITEMS) - 1
        assert progress and progress[-1] == len(ITEMS) and progress == sorted(progress)

        b = {k: result[k] for k in ("format", "version", "model", "created", "wfweb", "items")}
        radio.settings[reg("0083")] = bytes.fromhex("01")
        ws.send(json.dumps({"cmd": "setMenuRestore", "id": 6, "backup": b, "dryRun": True}))
        while True:
            msg = ws.recv(timeout=60)
            if isinstance(msg, bytes):
                continue
            m = json.loads(msg)
            if m.get("type") == "setMenuResult" and m.get("id") == 6:
                break
        assert [c["item"] for c in m["changes"]] == ["0083"]
        assert radio.settings[reg("0083")] == bytes.fromhex("01")
    finally:
        ws.close()
