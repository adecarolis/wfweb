"""Speech compressor: the COMP controls, and wfweb taking COMP out of its own
digital overs (#147).

wfweb's digital modes (FT8/FT4, JS8, TUNE, FreeDV/RADE, packet) transmit in
plain USB / LSB. Icom forces the compressor OFF only in the SSB data modes,
so a COMP the operator left on for voice would sit in the path. The server
switches it off before a digital key and back on after the unkey, and only
then. Runs on the IC-7300 mock: COMP is CI-V 16 44, its level 14 0E.

The same overs hold the rig's SSB transmit filter (TBW, CI-V 16 58: 0 WIDE /
1 MID / 2 NAR) at WIDE: on NAR, 500-2500 Hz, an FT8 tone outside that range
left the radio with next to no power. The rig keeps one TBW setting with COMP
on and one with COMP off, so the server reads it after the COMP switch, sets
WIDE, and puts the setting back before COMP returns.

tests/comp_browser.mjs is the in-browser check of the controls: the
Standalone bundle on its virtual rig, the FUNC COMP tile, the GAIN COMP row,
TUNE keying as a digital over and the transport's hold. It needs
playwright-core and a Chromium and is skipped without them.
"""

import json
import shutil
import subprocess
import time
from pathlib import Path

import pytest
import requests

from conftest import wfweb_output_tail, ws_connect

ROOT = Path(__file__).resolve().parents[1]

COMP_OFF = bytes([0x16, 0x44, 0x00])
COMP_ON = bytes([0x16, 0x44, 0x01])
PTT_ON = bytes([0x1C, 0x00, 0x01])
PTT_OFF = bytes([0x1C, 0x00, 0x00])
TBW_READ = bytes([0x16, 0x58])
TBW_WIDE = bytes([0x16, 0x58, 0x00])
TBW_MID = bytes([0x16, 0x58, 0x01])
TBW_NAR = bytes([0x16, 0x58, 0x02])


def _wait(predicate, timeout=6.0, what="condition", detail=None):
    # One look a second. Every status build re-queues each missing or stale
    # value (getCache()) as an immediate read. The mock ACKs the reads it does
    # not implement, which carries no value, so those were asked again on
    # every status build for good; a re-queued read went to the back of the
    # line and the compressor, last in the status, was never read (0 reads
    # of 16 44 in 12 s at 2 Hz here, at 1 Hz on the macOS runner). Now the
    # queue leaves a pending read where it is and backs off a read nobody
    # answers after three asks (countUnanswered), see test_queue.py.
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(1.0)
    pytest.fail(f"{what} not met within {timeout} s" + (f"\n{detail()}" if detail else ""))


def _frames_since(mock, mark):
    """CI-V commands wfweb sent since civ_log index mark, without the
    FE FE <to> <from> header and the FD trailer."""
    return [bytes(f[4:-1]) for f in mock.civ_log[mark:]]


def _set_tbw(mock, tbw):
    """Change the mock's filter settings as the rig's own TBW key would, and
    let a read of the old ones that is on its way (the tile's polling) land:
    the test is about the hold, not about a key pressed in the same 100 ms."""
    mock.tbw = list(tbw)
    time.sleep(1.0)


def _index(frames, cmd):
    return [i for i, f in enumerate(frames) if f == cmd]


@pytest.fixture
def rig(wfweb_7300_instance, mock_radio_7300):
    """The IC-7300 mock in USB with COMP on, the server's cache aware of it,
    and a WebSocket client. Leaves the mock in that state afterwards."""
    proc, web_port = wfweb_7300_instance
    status_url = f"http://127.0.0.1:{web_port + 1}/api/v1/radio/status"
    info_url = f"http://127.0.0.1:{web_port + 1}/api/v1/radio/info"
    mock = mock_radio_7300
    mock.set_mode(0x01)
    mock.compressor = 1
    mock.tbw = [1, 1]

    def status():
        return requests.get(status_url, timeout=2).json()

    def ready():
        st = status()
        return st.get("mode") == "USB" and st.get("compressor") is True
    def why():
        info = requests.get(info_url, timeout=2).json()
        info = info.get("info", info)
        comp_reads = sum(1 for f in mock.civ_log if bytes(f[4:6]) == bytes([0x16, 0x44]))
        return (f"status: {status()}\n"
                f"info: hasCompressor={info.get('hasCompressor')} rig={info.get('model', info.get('rigName'))}\n"
                f"mock: compressor={mock.compressor}, 16 44 frames received={comp_reads}\n"
                f"--- wfweb output (tail) ---\n{wfweb_output_tail(proc)}")
    _wait(ready, timeout=30.0, what="status reports USB and COMP on", detail=why)
    ws = ws_connect(web_port, proc)

    def send(**cmd):
        ws.send(json.dumps(cmd))

    yield mock, send, status, ws
    try:
        send(cmd="setPTT", value=False)
        ws.close()
    except Exception:
        pass
    mock.set_mode(0x01)
    mock.compressor = 1
    mock.tbw = [1, 1]


def test_info_and_status_carry_the_compressor(wfweb_7300_instance, rig):
    """The capability flags gate the FUNC COMP tile and the GAIN COMP row."""
    _, web_port = wfweb_7300_instance
    info = requests.get(f"http://127.0.0.1:{web_port + 1}/api/v1/radio/info", timeout=2).json()
    info = info.get("info", info)
    assert info.get("hasCompressor") is True
    assert info.get("hasCompLevel") is True
    _, _, status, _ = rig
    _wait(lambda: status().get("compLevel") == 128, what="status reports the COMP level")


def test_comp_tile_and_level_row_drive_the_rig(rig):
    """setCompressor switches 16 44, setCompLevel sets 14 0E, and the read-back
    brings the status along (a set alone does not refresh the cache)."""
    mock, send, status, _ = rig
    send(cmd="setCompressor", value=False)
    _wait(lambda: mock.compressor == 0, what="mock COMP off")
    _wait(lambda: status().get("compressor") is False, what="status follows COMP off")

    send(cmd="setCompLevel", value=51)
    _wait(lambda: mock._comp_level == 51, what="mock COMP level 51")
    _wait(lambda: status().get("compLevel") == 51, what="status follows the level")
    send(cmd="setCompLevel", value=128)
    _wait(lambda: mock._comp_level == 128, what="mock COMP level back to 128")


def test_digital_over_takes_comp_out_and_puts_it_back(rig):
    """COMP goes off before the key and back on after the unkey."""
    mock, send, _, _ = rig
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: PTT_ON in _frames_since(mock, mark), what="PTT on reaches the rig")
    assert mock.compressor == 0
    frames = _frames_since(mock, mark)
    assert _index(frames, COMP_OFF)[0] < _index(frames, PTT_ON)[0], "COMP must go off before the key"

    send(cmd="setPTT", value=False)
    _wait(lambda: mock.compressor == 1, what="COMP back on after the over")
    frames = _frames_since(mock, mark)
    assert _index(frames, PTT_OFF)[0] < _index(frames, COMP_ON)[0], "COMP must return after the unkey"


def test_voice_over_leaves_comp_alone(rig):
    """A plain PTT is the operator's voice: COMP stays as it is."""
    mock, send, _, _ = rig
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True)
    _wait(lambda: PTT_ON in _frames_since(mock, mark), what="PTT on reaches the rig")
    send(cmd="setPTT", value=False)
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
    time.sleep(0.5)
    assert COMP_OFF not in _frames_since(mock, mark)
    assert mock.compressor == 1


def test_set_is_not_held_up_by_refresh_reads(rig):
    """A set reaches the rig promptly while the status is polled. Before the
    queue backed off reads nobody answers, getCache() kept ~30 of them queued
    at immediate priority, 70 ms each on LAN, and a set behind them reached
    the rig ~2 s late here, past 6 s on the macOS runner. The backoff takes
    three asks of each read to set in, so the status is polled for a while
    first, as a browser would have been."""
    mock, send, status, _ = rig
    settle = time.monotonic() + 8.0
    while time.monotonic() < settle:
        status()
        time.sleep(0.25)
    total = 0.0
    for on in (False, True, False, True):
        want = COMP_ON if on else COMP_OFF
        mark = len(mock.civ_log)
        t0 = time.monotonic()
        send(cmd="setCompressor", value=on)
        while want not in _frames_since(mock, mark) and time.monotonic() - t0 < 10:
            status()
            time.sleep(0.05)
        took = time.monotonic() - t0
        assert want in _frames_since(mock, mark) and took < 2.0, f"COMP set took {took:.2f} s"
        total += took
    assert total < 4.0, f"four COMP sets took {total:.2f} s together"


def test_comp_left_off_is_never_switched_on(rig):
    """With COMP already off there is nothing to restore."""
    mock, send, status, _ = rig
    send(cmd="setCompressor", value=False)
    _wait(lambda: status().get("compressor") is False, what="status reports COMP off")
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: PTT_ON in _frames_since(mock, mark), what="PTT on reaches the rig")
    send(cmd="setPTT", value=False)
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
    time.sleep(0.5)
    assert COMP_ON not in _frames_since(mock, mark)
    assert mock.compressor == 0


def test_operator_wins_over_the_hold(rig):
    """COMP switched off by the operator mid-over stays off after it."""
    mock, send, _, _ = rig
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: PTT_ON in _frames_since(mock, mark), what="PTT on reaches the rig")
    send(cmd="setCompressor", value=False)
    send(cmd="setPTT", value=False)
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
    time.sleep(0.5)
    assert COMP_ON not in _frames_since(mock, mark)
    assert mock.compressor == 0


def test_comp_restored_when_keying_client_vanishes(rig):
    """A browser lost mid-over: the server unkeys (#144) and puts COMP back."""
    mock, send, _, ws = rig
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: mock.compressor == 0, what="COMP off for the over")
    ws.socket.close()
    ws.close()
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="server unkeys the vanished client")
    _wait(lambda: mock.compressor == 1, what="COMP back on")


def test_no_hold_outside_ssb(rig):
    """COMP only acts in SSB: an FM over (1200 bd packet) leaves it alone."""
    mock, send, status, _ = rig
    mock.set_mode(0x05)  # FM
    _wait(lambda: status().get("mode") == "FM", timeout=15.0, what="status reports FM")
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: PTT_ON in _frames_since(mock, mark), what="PTT on reaches the rig")
    send(cmd="setPTT", value=False)
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
    time.sleep(0.5)
    assert COMP_OFF not in _frames_since(mock, mark)
    assert mock.compressor == 1


def test_digital_over_holds_the_tx_filter_wide(rig):
    """NAR with COMP off, MID with COMP on, COMP on: the over runs with COMP
    off, so that setting is the one read, set to WIDE and put back, in this
    order: COMP off, read, key ... unkey, NAR back, COMP on. Twice: the second
    read brings the value the cache already holds, which the queue does not
    announce."""
    mock, send, _, _ = rig
    _set_tbw(mock, [2, 1])
    for n in (1, 2):
        mark = len(mock.civ_log)
        send(cmd="setPTT", value=True, digital=True)
        _wait(lambda: mock.tbw[0] == 0, what=f"over {n}: TBW WIDE for the over")
        frames = _frames_since(mock, mark)
        wide = _index(frames, TBW_WIDE)[0]
        reads = [i for i in _index(frames, TBW_READ) if i < wide]
        assert reads and reads[-1] > _index(frames, COMP_OFF)[0], "TBW must be read with COMP off"
        assert mock.tbw[1] == 1, "the COMP-on setting is not the over's"

        send(cmd="setPTT", value=False)
        _wait(lambda: mock.compressor == 1, what=f"over {n}: COMP back on")
        frames = _frames_since(mock, mark)
        assert mock.tbw == [2, 1], f"over {n}: TBW settings back as they were, got {mock.tbw}"
        assert _index(frames, PTT_OFF)[0] < _index(frames, TBW_NAR)[0] < _index(frames, COMP_ON)[0], \
            "TBW returns after the unkey and before COMP"


def test_tx_filter_already_wide_is_left_alone(rig):
    """WIDE needs no set, and nothing is put back after the over."""
    mock, send, status, _ = rig
    send(cmd="setCompressor", value=False)
    _wait(lambda: status().get("compressor") is False, what="status reports COMP off")
    _set_tbw(mock, [0, 2])
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: TBW_READ in _frames_since(mock, mark), what="TBW read")
    time.sleep(0.5)
    send(cmd="setPTT", value=False)
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
    time.sleep(0.5)
    sets = [f for f in _frames_since(mock, mark) if f[:2] == TBW_READ and len(f) == 3]
    assert not sets, f"no TBW set expected, got {sets}"
    assert mock.tbw == [0, 2]


def test_voice_over_leaves_the_tx_filter_alone(rig):
    """A plain PTT is the operator's voice: TBW is neither read nor set."""
    mock, send, _, _ = rig
    _set_tbw(mock, [2, 2])
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True)
    _wait(lambda: PTT_ON in _frames_since(mock, mark), what="PTT on reaches the rig")
    send(cmd="setPTT", value=False)
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
    time.sleep(0.5)
    assert not [f for f in _frames_since(mock, mark) if f[:2] == TBW_READ and len(f) == 3]
    assert mock.tbw == [2, 2]


def test_comp_switched_mid_over_moves_the_tx_filter_hold(rig):
    """The operator switches COMP on during the over (it was held off): the
    COMP-off filter goes back first, and the COMP-on one is held instead and
    restored at the end."""
    mock, send, status, _ = rig
    send(cmd="setCompressor", value=False)
    _wait(lambda: status().get("compressor") is False, what="status reports COMP off")
    _set_tbw(mock, [2, 1])
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: mock.tbw[0] == 0, what="COMP-off TBW WIDE for the over")
    send(cmd="setCompressor", value=True)
    _wait(lambda: mock.compressor == 1 and mock.tbw == [2, 0],
          what="COMP-off TBW back to NAR, COMP-on TBW WIDE", detail=lambda: f"tbw={mock.tbw}")
    send(cmd="setPTT", value=False)
    _wait(lambda: mock.tbw == [2, 1], what="COMP-on TBW back to MID", detail=lambda: f"tbw={mock.tbw}")
    assert mock.compressor == 1


def test_comp_switched_just_before_the_key_reads_the_right_filter(rig):
    """The operator switches COMP off and keys at once. The set is in the
    server's cache as it is queued, so the hold finds COMP "off" while the rig
    still has it on, and a filter read sent meanwhile is the COMP-on setting:
    the hold must wait for that set to go out before it asks. Here the tile's
    own read-back (200 ms after setTxBandwidth) is what sits ahead of the COMP
    set; several offsets, since the read must still be queued at the key. On
    the macOS runner any read did, behind its slower queue (2026-11-17)."""
    mock, send, status, _ = rig
    for delay in (0.17, 0.19, 0.20, 0.21, 0.22, 0.24):
        _set_tbw(mock, [2, 1])
        mark = len(mock.civ_log)
        send(cmd="setTxBandwidth", value=1)     # the COMP-on setting, as it is
        time.sleep(delay)
        send(cmd="setCompressor", value=False)
        send(cmd="setPTT", value=True, digital=True)
        _wait(lambda: mock.tbw[0] == 0, what="COMP-off TBW WIDE for the over",
              detail=lambda: f"tbw={mock.tbw}")
        send(cmd="setPTT", value=False)
        _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
        time.sleep(0.5)
        assert mock.tbw == [2, 1], f"delay {delay}: filters back as they were, got {mock.tbw}"
        assert mock.compressor == 0, "COMP stays as the operator left it"
        send(cmd="setCompressor", value=True)
        _wait(lambda: mock.compressor == 1 and status().get("compressor") is True,
              what="COMP back on for the next round")


def test_tx_filter_restored_when_keying_client_vanishes(rig):
    """A browser lost mid-over: the server unkeys and puts TBW back."""
    mock, send, _, ws = rig
    _set_tbw(mock, [2, 1])
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: mock.tbw[0] == 0, what="TBW WIDE for the over")
    ws.socket.close()
    ws.close()
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="server unkeys the vanished client")
    _wait(lambda: mock.tbw == [2, 1] and mock.compressor == 1, what="TBW and COMP back")


def test_tbw_tile_reads_and_sets_the_filter_of_the_comp_state(wfweb_7300_instance, rig):
    """The FUNC TBW tile: rigInfo says the rig has the filter, the status
    carries the setting in use, COMP switched brings the other setting, and
    setTxBandwidth writes the one in use."""
    _, web_port = wfweb_7300_instance
    info = requests.get(f"http://127.0.0.1:{web_port + 1}/api/v1/radio/info", timeout=2).json()
    info = info.get("info", info)
    assert info.get("hasTxBandwidth") is True
    mock, send, _, ws = rig
    _set_tbw(mock, [2, 1])

    def tbw():
        """What the WebSocket status says (the REST status does not carry it)."""
        send(cmd="getStatus")
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                msg = json.loads(ws.recv(timeout=1))
            except TimeoutError:
                continue
            if isinstance(msg, dict) and msg.get("type") == "status":
                return msg.get("txBandwidth")
        return None

    _wait(lambda: tbw() == 1, timeout=30.0, what="status: MID, the COMP-on setting")
    send(cmd="setCompressor", value=False)
    _wait(lambda: tbw() == 2, timeout=15.0, what="COMP off: status follows to NAR")
    send(cmd="setTxBandwidth", value=0)
    _wait(lambda: mock.tbw == [0, 1], what="setTxBandwidth writes the COMP-off setting")
    _wait(lambda: tbw() == 0, what="status says WIDE")
    send(cmd="setCompressor", value=True)
    _wait(lambda: tbw() == 1, timeout=15.0, what="COMP on: status back to MID")


def test_tbw_set_by_the_operator_mid_over_stays(rig):
    """The tile pressed during a digital over: that setting is not undone."""
    mock, send, status, _ = rig
    send(cmd="setCompressor", value=False)
    _wait(lambda: status().get("compressor") is False, what="status reports COMP off")
    _set_tbw(mock, [2, 1])
    mark = len(mock.civ_log)
    send(cmd="setPTT", value=True, digital=True)
    _wait(lambda: mock.tbw[0] == 0, what="TBW WIDE for the over")
    send(cmd="setTxBandwidth", value=1)
    _wait(lambda: mock.tbw[0] == 1, what="the operator's MID reaches the rig")
    send(cmd="setPTT", value=False)
    _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
    time.sleep(1.0)
    assert mock.tbw == [1, 1], f"MID must stay, got {mock.tbw}"


def test_first_key_after_connect_reaches_the_rig(wfweb_7300_instance, mock_radio_7300):
    """A status read of the PTT state must not delete a queued key. The mock
    never answers that read, so the 5 Hz status re-queues it on every tick;
    before the queue fix a key sent right after connecting was lost."""
    proc, web_port = wfweb_7300_instance
    mock = mock_radio_7300
    ws = ws_connect(web_port, proc)
    try:
        mark = len(mock.civ_log)
        ws.send(json.dumps({"cmd": "setPTT", "value": True}))
        _wait(lambda: PTT_ON in _frames_since(mock, mark), what="PTT on reaches the rig")
    finally:
        ws.send(json.dumps({"cmd": "setPTT", "value": False}))
        _wait(lambda: PTT_OFF in _frames_since(mock, mark), what="PTT off reaches the rig")
        ws.close()


def _node(script, *args, timeout=300):
    return subprocess.run(["node", str(ROOT / "tests" / script), *args],
                          check=False, capture_output=True, text=True, timeout=timeout)


def test_comp_browser():
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("comp_browser.mjs", "--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    result = _node("comp_browser.mjs")
    assert result.returncode == 0, result.stdout + result.stderr


def test_tbw_browser():
    """The Standalone transport's hold of the transmit filter, on the Web
    Serial transport against a fake IC-7300."""
    if not shutil.which("node"):
        pytest.skip("node unavailable")
    probe = _node("tbw_browser.mjs", "--probe", timeout=60)
    if probe.returncode != 0:
        pytest.skip("no playwright-core / Chromium: " + probe.stdout.strip())
    result = _node("tbw_browser.mjs")
    assert result.returncode == 0, result.stdout + result.stderr
