"""RX audio from a LAN rig must not depend on the rig's clock (#161).

The rig's ping requests carry its uptime in ms. The audio stream used to
compare that clock with the server's own, synced once per process, and to drop
one audio packet in six for as long as the rig's clock was more than 150 ms
behind the prediction. Two crystals a few ppm apart get there in hours, so a
server left running lost its RX audio for good.

The mock streams 48 kHz audio in the radio's packets and pings with a clock
that is made to fall behind; every packet has to reach the browser.
"""

import asyncio
import json
import struct
import time

import pytest

from conftest import (_find_free_port, _launch_wfweb, _run_on_loop, _stop_wfweb,
                      ws_connect)
from mock_radio import (_HEADER_SIZE, _PING_SIZE, _PT_ARE_YOU_READY,
                        _PT_ARE_YOU_THERE, _PT_DATA, _PT_I_AM_HERE, _PT_PING,
                        MockIcomRadio)

AUDIO_HEADER = 0x18
# What an Icom sends for 20 ms of 48 kHz 16-bit mono.
SLICE = (1364, 556)
# One value per packet, carried in all its samples: 1000 values (10 s) before
# it repeats, far enough apart for the format conversion not to blur them.
VALUES, STEP = 1000, 16


class ClockRadio(MockIcomRadio):
    """MockIcomRadio with an audio stream and pings on a clock of its own."""

    def __init__(self):
        super().__init__()
        self.clock = lambda elapsed_ms: elapsed_ms
        self.packets = 0
        self._audio_seq = 1
        self._task = None

    def _on_packet(self, data, addr, label, proto):
        super()._on_packet(data, addr, label, proto)
        if label != "audio" or len(data) != _HEADER_SIZE:
            return
        ptype = struct.unpack_from("<H", data, 4)[0]
        sender = struct.unpack_from("<I", data, 8)[0]
        if ptype == _PT_ARE_YOU_THERE:
            proto.send(self._ctrl_pkt(_PT_I_AM_HERE, 0, sender), addr)
        elif ptype == _PT_ARE_YOU_READY and self._task is None:
            self._task = asyncio.get_running_loop().create_task(
                self._stream(proto, addr, sender))

    async def stop(self):
        if self._task is not None:
            self._task.cancel()
        await super().stop()

    async def _stream(self, proto, addr, client):
        start = asyncio.get_running_loop().time()
        ticks = 0
        while True:
            elapsed = asyncio.get_running_loop().time() - start
            if ticks % 5 == 0:                  # ten pings a second
                proto.send(self._ping_request(int(self.clock(elapsed * 1000)),
                                              ticks // 5, client), addr)
            for size in SLICE:
                proto.send(self._audio_packet(size, client), addr)
            ticks += 1
            await asyncio.sleep(max(0, start + ticks * 0.02
                                    - asyncio.get_running_loop().time()))

    def _ping_request(self, ms, seq, client):
        pkt = bytearray(_PING_SIZE)
        struct.pack_into("<IHHII", pkt, 0, _PING_SIZE, _PT_PING, seq & 0xFFFF,
                         self.radio_id, client)
        struct.pack_into("<I", pkt, 0x11, (3_600_000 + ms) & 0xFFFFFFFF)
        return bytes(pkt)

    def _audio_packet(self, size, client):
        value = (self.packets % VALUES + 1) * STEP
        pkt = bytearray(AUDIO_HEADER) + struct.pack("<h", value) * (size // 2)
        struct.pack_into("<IHHII", pkt, 0, len(pkt), _PT_DATA,
                         self._audio_seq, self.radio_id, client)
        struct.pack_into("<HHHH", pkt, 0x10, 0x9781, 0, 0, 0)
        struct.pack_into(">H", pkt, 0x16, size)
        self.packets += 1
        self._audio_seq = self._audio_seq % 0xFFFF + 1
        return bytes(pkt)


def _slow(ms):
    """The rig's clock loses 100 ms a second: hours of drift in seconds."""
    return ms * 0.9


def _stepped(ms):
    """One change of 1 s that stays: a slower route, a rig that restarted."""
    return ms if ms < 1000 else ms - 1000


@pytest.fixture
def clock_radio(event_loop, tmp_path, request):
    radio = ClockRadio()
    radio.clock = request.param
    _run_on_loop(event_loop, radio.start())
    proc = None
    try:
        proc, port = _launch_wfweb(radio, event_loop, _find_free_port(), "0x98",
                                   str(tmp_path / "home"))
        yield radio, proc, port
    finally:
        if proc is not None:
            _stop_wfweb(proc)
        _run_on_loop(event_loop, radio.stop())


@pytest.mark.parametrize("clock_radio", [_slow, _stepped], indirect=True,
                         ids=["slow", "stepped"])
def test_every_audio_packet_reaches_the_browser(clock_radio):
    radio, proc, port = clock_radio
    deadline = time.monotonic() + 30
    while radio.packets < 600:                  # 6 s: the clock is far behind
        assert time.monotonic() < deadline, "the server never opened the audio stream"
        time.sleep(0.1)

    values = []
    with ws_connect(port, proc) as ws:
        ws.send(json.dumps({"cmd": "enableAudio", "value": True}))
        end = time.monotonic() + 4
        while time.monotonic() < end:
            try:
                msg = ws.recv(timeout=0.5)
            except TimeoutError:
                continue
            if isinstance(msg, bytes) and msg[:1] == b"\x02":
                for s in struct.unpack(f"<{(len(msg) - 6) // 2}h", msg[6:]):
                    v = round(s / STEP)
                    if not values or values[-1] != v:
                        values.append(v)

    assert len(values) > 300, f"hardly any audio: {len(values)} packets in 4 s"
    missing = sum((b - a - 1) % VALUES for a, b in zip(values, values[1:]))
    assert missing == 0, f"{missing} of {len(values) + missing} audio packets lost"
