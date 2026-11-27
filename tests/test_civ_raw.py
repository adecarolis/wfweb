"""Raw CI-V passthrough: POST /api/v1/radio/civ and the WebSocket civRaw.

Runs against the LAN mock (IC-7610, CI-V 0x98).  The mock keeps what is
written to a 1A 05 register and reads it back, and answers to whatever
controller address sent the frame, as a real Icom does.
"""

import json
import time

import requests

RADIO = 0x98
CTRL = 0xE2
REG = 0x0061          # any 1A 05 register; the mock does not care which


def civ(rest_url, hexstr, timeout=5):
    return requests.post(f"{rest_url}/civ", json={"hex": hexstr}, timeout=timeout)


def test_read_set_read(rest_url, mock_radio):
    mock_radio.settings[REG] = bytes([0x00])
    r = civ(rest_url, "1A 05 00 61")
    assert r.status_code == 200, r.text
    d = r.json()
    assert d["request"] == "FE FE 98 E2 1A 05 00 61 FD"
    assert d["reply"] == "data"
    assert d["frames"] == ["FE FE E2 98 1A 05 00 61 00 FD"]
    assert d["data"] == "00"

    r = civ(rest_url, "1a0500 61 01")
    assert r.status_code == 200, r.text
    assert r.json()["reply"] == "ok"
    assert mock_radio.settings[REG] == bytes([0x01])

    d = civ(rest_url, "1A 05 00 61").json()
    assert d["data"] == "01"


def test_sent_from_its_own_address(rest_url, mock_radio):
    civ(rest_url, "1A 05 00 61")
    raw = [f for f in mock_radio.civ_log if len(f) > 4 and f[3] == CTRL]
    assert raw, "no frame from E2 reached the radio"
    assert raw[-1] == bytes([0xFE, 0xFE, RADIO, CTRL, 0x1A, 0x05, 0x00, 0x61, 0xFD])


def test_whole_frame_is_taken(rest_url, mock_radio):
    mock_radio.settings[REG] = bytes([0x01])
    r = civ(rest_url, "FE FE 98 E0 1A 05 00 61 FD")
    assert r.status_code == 200, r.text
    # The from-address is always ours.
    assert r.json()["request"] == "FE FE 98 E2 1A 05 00 61 FD"
    assert r.json()["data"] == "01"


def test_ng_is_reported(rest_url, mock_radio, monkeypatch):
    orig = mock_radio.dispatch_civ

    def nak(cmd, payload, from_addr):
        if from_addr == CTRL:
            return mock_radio.civ_nak(from_addr, RADIO)
        return orig(cmd, payload, from_addr)

    monkeypatch.setattr(mock_radio, "dispatch_civ", nak)
    r = civ(rest_url, "1A 05 00 61 05")
    assert r.status_code == 200, r.text
    assert r.json()["reply"] == "ng"


def test_late_reply_is_not_taken_for_the_next(rest_url, mock_radio, monkeypatch):
    """A reply that comes after its request timed out arrives while the next
    request waits: it echoes another command and must be ignored."""
    mock_radio.settings[REG] = bytes([0x01])
    mock_radio.settings[0x0062] = bytes([0x00])
    orig = mock_radio.dispatch_civ

    def stale_first(cmd, payload, from_addr):
        r = orig(cmd, payload, from_addr)
        if from_addr == CTRL and cmd == 0x1A and payload == bytes([0x05, 0x00, 0x61]):
            late = mock_radio.civ_frame(CTRL, RADIO, 0x1A, data=bytes([0x05, 0x00, 0x62, 0x00]))
            return late + r
        return r

    monkeypatch.setattr(mock_radio, "dispatch_civ", stale_first)
    d = civ(rest_url, "1A 05 00 61").json()
    assert d["reply"] == "data" and d["data"] == "01", d
    assert d["body"] == "1A 05 00 61 01"


def test_timeout(rest_url, mock_radio, monkeypatch):
    orig = mock_radio.dispatch_civ

    def silent(cmd, payload, from_addr):
        if from_addr == CTRL:
            return None
        return orig(cmd, payload, from_addr)

    monkeypatch.setattr(mock_radio, "dispatch_civ", silent)
    t0 = time.monotonic()
    r = civ(rest_url, "1A 05 00 61")
    assert r.status_code == 504, r.text
    assert r.json()["reply"] == "timeout"
    assert 0.8 < time.monotonic() - t0 < 4


def test_requests_wait_their_turn(rest_url, mock_radio):
    """Several callers at once: each gets the answer to its own question."""
    from concurrent.futures import ThreadPoolExecutor
    regs = [0x0061, 0x0062, 0x0063, 0x0064, 0x0065]
    for i, reg in enumerate(regs):
        mock_radio.settings[reg] = bytes([i])
    with ThreadPoolExecutor(len(regs)) as ex:
        res = list(ex.map(lambda reg: civ(rest_url, f"1A 05 {reg >> 8:02X} {reg & 0xFF:02X}"), regs))
    for i, (reg, r) in enumerate(zip(regs, res)):
        assert r.status_code == 200, r.text
        d = r.json()
        assert d["body"].startswith(f"1A 05 {reg >> 8:02X} {reg & 0xFF:02X}"), d
        assert d["data"] == f"{i:02X}", d


def test_malformed_is_refused(rest_url):
    for bad in ["", "1A 0", "1A ZZ", "1A FD 00", "FE FE 98 E0 1A 05",
                "FE FE 12 E0 1A FD", "FE FE 98 E0 1A FD FE FE 98 E0 1A FD",
                "00 " * 65]:
        r = civ(rest_url, bad)
        assert r.status_code == 400, (bad, r.text)
        assert "error" in r.json()
    r = requests.post(f"{rest_url}/civ", data="not json", timeout=5)
    assert r.status_code == 400
    r = requests.get(f"{rest_url}/civ", timeout=5)
    assert r.status_code == 405


def test_transmit_and_power_are_refused(rest_url, mock_radio):
    before = len(mock_radio.civ_log)
    for cmd in ["1C 00 01", "1C 00", "1C 01 02", "17 43 51", "18 00", "29 00 1C 00 01"]:
        r = civ(rest_url, cmd)
        assert r.status_code == 403, (cmd, r.text)
    sent = [f for f in mock_radio.civ_log[before:] if len(f) > 4 and f[3] == CTRL]
    assert not sent, sent


def test_usb_with_echo(usb_rest_url, serial_mock, monkeypatch):
    """Over the serial port, the way an IC-7300 on USB is driven.  With
    CI-V USB Echo Back on, the radio sends our own frame back first: it is
    addressed to the radio, so it must not be taken for the reply."""
    serial_mock.settings[REG] = bytes([0x00])
    orig = serial_mock.parse_and_dispatch

    def echo_then_reply(frame):
        reply = orig(frame)
        if len(frame) > 4 and frame[3] == CTRL and reply:
            return bytes(frame) + reply
        return reply

    monkeypatch.setattr(serial_mock, "parse_and_dispatch", echo_then_reply)
    r = civ(usb_rest_url, "1A 05 00 61")
    assert r.status_code == 200, r.text
    d = r.json()
    assert d["frames"] == ["FE FE E2 98 1A 05 00 61 00 FD"], d
    assert d["data"] == "00"
    r = civ(usb_rest_url, "1A 05 00 61 01")
    assert r.json()["reply"] == "ok", r.text
    assert serial_mock.settings[REG] == bytes([0x01])


def test_websocket(wfweb_instance, mock_radio):
    from conftest import ws_connect
    proc, web_port = wfweb_instance
    mock_radio.settings[REG] = bytes([0x01])
    ws = ws_connect(web_port, proc)
    try:
        ws.send(json.dumps({"cmd": "civRaw", "id": 7, "hex": "1A 05 00 61"}))
        ws.send(json.dumps({"cmd": "civRaw", "id": "x", "hex": "1C 00 01"}))
        got = {}
        deadline = time.monotonic() + 5
        while len(got) < 2:
            assert time.monotonic() < deadline, got
            msg = ws.recv(timeout=5)
            if isinstance(msg, bytes):
                continue
            m = json.loads(msg)
            if m.get("type") == "civRawReply":
                got[m["id"]] = m
        assert got[7]["reply"] == "data" and got[7]["data"] == "01"
        assert got["x"]["reply"] == "error" and got["x"]["status"] == 403
    finally:
        ws.close()
