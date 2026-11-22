"""P.AMP / ATT state reaches a page that connects after it was set.

The page drives one P.AMP/ATT button (OFF -> ATT -> PRE1 -> PRE2 -> OFF) off
its own idea of both states. The full status a client gets on connect carried
the preamp but not the attenuator, so a page that loaded with ATT already on
read OFF: its first press asked for the ATT the rig already had, nothing
changed, no update came back, and the button stayed dead on every press
(IC-7300 on 10 m, 2026-11-07). Runs on the IC-7300 mock: ATT is CI-V 11,
one BCD byte of dB.
"""

import json
import time

import requests

from conftest import wfweb_output_tail, ws_connect


def _wait(predicate, timeout, what, detail):
    end = time.time() + timeout
    while time.time() < end:
        if predicate():
            return
        time.sleep(0.2)
    raise AssertionError(f"timed out waiting for {what}\n{detail()}")


def test_connect_status_carries_the_attenuator(wfweb_7300_instance, mock_radio_7300):
    proc, web_port = wfweb_7300_instance
    base = f"http://127.0.0.1:{web_port + 1}/api/v1/radio"
    mock = mock_radio_7300
    mock.attenuator = 0x20  # set on the radio before any page connects

    def rx():
        r = requests.get(f"{base}/rx", timeout=2)
        return r.json() if r.text else {}  # empty until the cache fills

    def detail():
        return (f"rx: {rx()}\nmock attenuator byte: {mock.attenuator:#04x}\n"
                f"--- wfweb output (tail) ---\n{wfweb_output_tail(proc)}")

    try:
        _wait(lambda: rx().get("attenuator") == 20, 30.0,
              "the server to read ATT 20 dB", detail)

        status = requests.get(f"{base}/status", timeout=2).json()
        status = status.get("status", status)
        assert status.get("attenuator") == 20, status

        ws = ws_connect(web_port, proc)
        try:
            first = None
            end = time.time() + 10
            while time.time() < end and first is None:
                msg = ws.recv(timeout=end - time.time())
                if isinstance(msg, str):
                    m = json.loads(msg)
                    if m.get("type") == "status":
                        first = m
            assert first is not None, "no status message on connect"
            assert first.get("attenuator") == 20, first
        finally:
            ws.close()
    finally:
        mock.attenuator = 0
        _wait(lambda: rx().get("attenuator") == 0, 30.0,
              "ATT back to off", detail)
