"""A memory recall from memory mode reaches the rig in the order it was queued.

The IC-9700 has memory groups but no group-select command: the channel
select acts on the bank of the band the VFO is on. So recallMemoryOnRig()
queues, when the rig is already in memory mode, VFO mode (bare CI-V 07),
a frequency in the group's band, the channel select (08 <ch>) and memory
mode (bare 08). Since an immediate item with a value goes out ahead of the
plain reads queued before it (c8018d63), the bare 07, which carries no
value, was taken for a read and went out last: the rig loaded the channel
and went back to the VFO, with the channel's frequency and none of its tone
or duplex, while the page showed MEM. Recalling from VFO mode queues no 07,
so the first recall worked and every one after it failed (PR #164).

Runs on a LAN mock answering as an IC-9700 (CI-V 0xA2).
"""

from __future__ import annotations

import json
import time

import pytest

from conftest import (_find_free_port, _launch_wfweb, _run_on_loop,
                      _stop_wfweb, wfweb_output_tail, ws_connect)
from mock_radio import MockIcomRadio

IC9700 = 0xA2

VFO_MODE = bytes([0x07])
MEMORY_MODE = bytes([0x08])


def _frames_since(mock, mark):
    """CI-V commands wfweb sent since civ_log index mark, without the
    FE FE <to> <from> header and the FD trailer."""
    return [bytes(f[4:-1]) for f in mock.civ_log[mark:]]


def _is_channel_select(f):
    return len(f) > 1 and f[0] == 0x08 and f[1] != 0xA0


def _is_freq_set(f):
    # 05 <freq>, or 25 <rx> <freq> on the selected / unselected slot
    return (f[:1] == b"\x05" and len(f) == 6) or (f[:1] == b"\x25" and len(f) == 7)


@pytest.fixture(scope="module")
def ic9700(event_loop, tmp_path_factory):
    mock = MockIcomRadio(radio_addr=IC9700)
    _run_on_loop(event_loop, mock.start())
    home = str(tmp_path_factory.mktemp("wfweb-9700"))
    proc, web_port = _launch_wfweb(mock, event_loop, _find_free_port(), "0xA2", home)
    yield mock, proc, web_port
    _stop_wfweb(proc)
    _run_on_loop(event_loop, mock.stop())


def _recall(mock, proc, ws, channel, group, expect):
    """Recall a channel and return the CI-V frames it put on the wire, once
    every bare select in expect has gone out (whatever order they took: the
    bug sent the 07 last, behind the reads the recall queues)."""
    mark = len(mock.civ_log)
    ws.send(json.dumps({"cmd": "recallMemory", "channel": channel, "group": group}))
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        frames = _frames_since(mock, mark)
        if all(e in frames for e in expect):
            time.sleep(0.5)  # anything queued behind them
            return _frames_since(mock, mark)
        time.sleep(0.05)
    pytest.fail(f"recall of channel {channel} never sent {[e.hex() for e in expect]}\n"
                f"frames: {[f.hex(' ') for f in _frames_since(mock, mark)]}\n"
                f"--- wfweb output (tail) ---\n{wfweb_output_tail(proc)}")


def test_recall_from_memory_mode_leaves_the_rig_in_memory_mode(ic9700):
    mock, proc, web_port = ic9700
    ws = ws_connect(web_port, proc)
    try:
        # From the VFO: no 07, just the band, the channel and memory mode.
        first = _recall(mock, proc, ws, channel=1, group=1, expect=(MEMORY_MODE,))
        assert VFO_MODE not in first, [f.hex(" ") for f in first]

        # Now in memory mode: the recall goes back to the VFO first, and the
        # rig must end on the memory-mode select, not on the VFO.
        frames = _recall(mock, proc, ws, channel=2, group=1,
                         expect=(VFO_MODE, MEMORY_MODE))
        shown = [f.hex(" ") for f in frames]
        vfo = frames.index(VFO_MODE)
        mem = len(frames) - 1 - frames[::-1].index(MEMORY_MODE)
        chan = [i for i, f in enumerate(frames) if _is_channel_select(f)]
        freq = [i for i, f in enumerate(frames) if _is_freq_set(f)]
        assert chan, f"no channel select: {shown}"
        assert freq, f"no frequency set: {shown}"
        assert vfo < freq[0] < chan[0] < mem, f"recall went out out of order: {shown}"
        assert VFO_MODE not in frames[mem:], f"VFO mode sent after memory mode: {shown}"
    finally:
        ws.close()
