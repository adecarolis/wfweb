"""Mock Icom radio UDP server for integration testing wfweb.

Emulates an IC-7610 over UDP so that wfweb can connect, authenticate,
and exchange CI-V commands without real hardware.

Two asyncio datagram servers are started:
  * control port  - authentication handshake, pings, token renewal
  * CI-V port     - CI-V command / response exchange

Usage::

    server = MockIcomRadio()
    await server.start()
    # ... point wfweb at server.control_port ...
    await server.stop()
"""

from __future__ import annotations

import asyncio
import logging
import struct

from mock_civ import CivRadioBase, RADIO_IC7610_ADDR

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# UDP protocol constants
# ---------------------------------------------------------------------------

_HEADER_SIZE = 0x10
_PING_SIZE = 0x15
_CIV_HEADER_SIZE = 0x15

_PT_DATA = 0x00
_PT_ARE_YOU_THERE = 0x03
_PT_I_AM_HERE = 0x04
_PT_DISCONNECT = 0x05
_PT_ARE_YOU_READY = 0x06
_PT_PING = 0x07


# ---------------------------------------------------------------------------
# asyncio DatagramProtocol
# ---------------------------------------------------------------------------


class _MockProtocol(asyncio.DatagramProtocol):
    def __init__(self, owner: MockIcomRadio, label: str) -> None:
        self._owner = owner
        self._label = label
        self._transport: asyncio.DatagramTransport | None = None

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:
        self._transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            self._owner._on_packet(data, addr, self._label, self)
        except Exception:
            logger.exception("Mock %s: unhandled error", self._label)

    def error_received(self, exc: Exception) -> None:
        logger.debug("Mock %s UDP error: %s", self._label, exc)

    def connection_lost(self, exc: Exception | None) -> None:
        pass

    def send(self, data: bytes, addr: tuple[str, int]) -> None:
        if self._transport is not None and not self._transport.is_closing():
            self._transport.sendto(data, addr)


# ---------------------------------------------------------------------------
# MockIcomRadio
# ---------------------------------------------------------------------------


class MockIcomRadio(CivRadioBase):
    """Asyncio UDP server emulating an IC-7610 for integration testing.

    Inherits CI-V command handling from CivRadioBase. This class adds
    the Icom LAN UDP transport (control + CI-V ports).
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        radio_addr: int = RADIO_IC7610_ADDR,
    ) -> None:
        super().__init__(radio_addr)
        self._host = host

        # UDP transports
        self._ctrl_udp: asyncio.DatagramTransport | None = None
        self._civ_udp: asyncio.DatagramTransport | None = None
        self._audio_udp: asyncio.DatagramTransport | None = None

        # Actual bound ports
        self._actual_ctrl_port: int = 0
        self._actual_civ_port: int = 0
        self._actual_audio_port: int = 0

        # Connection state
        self.radio_id: int = 0xDEADBEEF
        self.token: int = 0x12345678
        self._ctrl_client_id: int = 0
        self._civ_client_id: int = 0
        self._ctrl_seq: int = 1
        self._civ_seq: int = 1

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    async def start(self) -> None:
        loop = asyncio.get_running_loop()

        ctrl_transport, _ = await loop.create_datagram_endpoint(
            lambda: _MockProtocol(self, "ctrl"),
            local_addr=(self._host, 0),
        )
        self._ctrl_udp = ctrl_transport
        self._actual_ctrl_port = ctrl_transport.get_extra_info("sockname")[1]

        civ_transport, _ = await loop.create_datagram_endpoint(
            lambda: _MockProtocol(self, "civ"),
            local_addr=(self._host, 0),
        )
        self._civ_udp = civ_transport
        self._actual_civ_port = civ_transport.get_extra_info("sockname")[1]

        audio_transport, _ = await loop.create_datagram_endpoint(
            lambda: _MockProtocol(self, "audio"),
            local_addr=(self._host, 0),
        )
        self._audio_udp = audio_transport
        self._actual_audio_port = audio_transport.get_extra_info("sockname")[1]

        logger.info(
            "MockIcomRadio started — ctrl=%d civ=%d audio=%d",
            self._actual_ctrl_port, self._actual_civ_port, self._actual_audio_port,
        )

    async def stop(self) -> None:
        for udp in (self._ctrl_udp, self._civ_udp, self._audio_udp):
            if udp is not None:
                udp.close()
        self._ctrl_udp = self._civ_udp = self._audio_udp = None

    @property
    def control_port(self) -> int:
        return self._actual_ctrl_port

    @property
    def civ_port(self) -> int:
        return self._actual_civ_port

    @property
    def audio_port(self) -> int:
        return self._actual_audio_port

    # ------------------------------------------------------------------
    # Packet dispatch
    # ------------------------------------------------------------------

    def _on_packet(self, data: bytes, addr: tuple[str, int],
                   label: str, proto: _MockProtocol) -> None:
        if len(data) < _HEADER_SIZE:
            return
        ptype = struct.unpack_from("<H", data, 4)[0]
        sender_id = struct.unpack_from("<I", data, 8)[0]

        if label == "ctrl":
            self._ctrl_client_id = sender_id
            self._handle_ctrl(data, addr, ptype, sender_id, proto)
        elif label == "civ":
            self._civ_client_id = sender_id
            self._handle_civ(data, addr, ptype, sender_id, proto)

    # ------------------------------------------------------------------
    # Control port
    # ------------------------------------------------------------------

    def _handle_ctrl(self, data, addr, ptype, sender_id, proto):
        n = len(data)

        if n == _HEADER_SIZE and ptype == _PT_ARE_YOU_THERE:
            proto.send(self._ctrl_pkt(_PT_I_AM_HERE, 0, sender_id), addr)
            return
        if n == _HEADER_SIZE and ptype == _PT_ARE_YOU_READY:
            proto.send(self._ctrl_pkt(_PT_ARE_YOU_READY, 0, sender_id), addr)
            return
        if n == _PING_SIZE and ptype == _PT_PING and data[0x10] == 0x00:
            proto.send(self._ping_reply(data, sender_id), addr)
            return
        if n == 0x80:
            proto.send(self._login_response(data, sender_id), addr)
            return
        if n == 0x40 and data[0x15] == 0x02:
            proto.send(self._capabilities_packet(sender_id), addr)
            proto.send(self._conninfo_packet(sender_id), addr)
            return
        if n == 0x40 and data[0x15] in (0x01, 0x05):
            return
        if n == 0x90 and data[0x15] == 0x03:
            proto.send(self._status_response(sender_id), addr)
            return
        if n == _HEADER_SIZE and ptype == _PT_DISCONNECT:
            return

    # ------------------------------------------------------------------
    # CI-V port
    # ------------------------------------------------------------------

    def _handle_civ(self, data, addr, ptype, sender_id, proto):
        n = len(data)

        if n == _HEADER_SIZE and ptype == _PT_ARE_YOU_THERE:
            proto.send(self._ctrl_pkt(_PT_I_AM_HERE, 0, sender_id), addr)
            return
        if n == _HEADER_SIZE and ptype == _PT_ARE_YOU_READY:
            proto.send(self._ctrl_pkt(_PT_ARE_YOU_READY, 0, sender_id), addr)
            return
        if n == _PING_SIZE and ptype == _PT_PING and data[0x10] == 0x00:
            proto.send(self._ping_reply(data, sender_id), addr)
            return
        if n == 0x16:
            if data[0x15] == 0x04:
                civ = self.civ_frame(to=0x00, frm=self._radio_addr, cmd=0x00)
                proto.send(self._wrap_civ(civ, sender_id), addr)
            return
        if n == _HEADER_SIZE and ptype == _PT_DISCONNECT:
            return
        if ptype == _PT_DATA and n > _CIV_HEADER_SIZE:
            self._handle_civ_data(data, addr, sender_id, proto)

    def _handle_civ_data(self, data, addr, sender_id, proto):
        datalen = struct.unpack_from("<H", data, 0x11)[0]
        end = _CIV_HEADER_SIZE + datalen
        if end > len(data):
            return
        frame = data[_CIV_HEADER_SIZE:end]

        response = self.parse_and_dispatch(frame)
        if response is not None:
            proto.send(self._wrap_civ(response, sender_id), addr)

    # ------------------------------------------------------------------
    # UDP packet builders
    # ------------------------------------------------------------------

    def _wrap_civ(self, civ_frame: bytes, client_id: int) -> bytes:
        total = _CIV_HEADER_SIZE + len(civ_frame)
        pkt = bytearray(total)
        struct.pack_into("<I", pkt, 0x00, total)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<H", pkt, 0x06, self._civ_seq)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, client_id)
        pkt[0x10] = 0xC1
        struct.pack_into("<H", pkt, 0x11, len(civ_frame))
        struct.pack_into(">H", pkt, 0x13, self._civ_seq)
        pkt[_CIV_HEADER_SIZE:] = civ_frame
        self._civ_seq = (self._civ_seq + 1) & 0xFFFF
        return bytes(pkt)

    def _ctrl_pkt(self, ptype, seq, client_id):
        pkt = bytearray(_HEADER_SIZE)
        struct.pack_into("<I", pkt, 0x00, _HEADER_SIZE)
        struct.pack_into("<H", pkt, 0x04, ptype)
        struct.pack_into("<H", pkt, 0x06, seq)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, client_id)
        return bytes(pkt)

    def _ping_reply(self, data, client_id):
        pkt = bytearray(_PING_SIZE)
        struct.pack_into("<I", pkt, 0x00, _PING_SIZE)
        struct.pack_into("<H", pkt, 0x04, _PT_PING)
        seq = struct.unpack_from("<H", data, 6)[0]
        struct.pack_into("<H", pkt, 0x06, seq)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, client_id)
        pkt[0x10] = 0x01
        pkt[0x11:0x15] = data[0x11:0x15]
        return bytes(pkt)

    def _login_response(self, data, sender_id):
        tok_request = struct.unpack_from("<H", data, 0x1A)[0]
        pkt = bytearray(0x60)
        struct.pack_into("<I", pkt, 0x00, 0x60)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, sender_id)
        struct.pack_into("<H", pkt, 0x1A, tok_request)
        struct.pack_into("<I", pkt, 0x1C, self.token)
        pkt[0x40:0x44] = b"FTTH"
        return bytes(pkt)

    def _capabilities_packet(self, sender_id):
        _CAP_HEADER, _RADIO_CAP = 0x42, 0x66
        total = _CAP_HEADER + _RADIO_CAP
        pkt = bytearray(total)
        struct.pack_into("<I", pkt, 0x00, total)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, sender_id)
        struct.pack_into("<H", pkt, 0x40, 1)
        r = _CAP_HEADER
        pkt[r:r + 16] = bytes(range(0x01, 0x11))
        name = b"IC-7610\x00"
        pkt[r + 0x10:r + 0x10 + len(name)] = name
        audio = b"IC-7610 USB Audio\x00"
        pkt[r + 0x30:r + 0x30 + len(audio)] = audio
        pkt[r + 0x52] = self._radio_addr
        struct.pack_into("<H", pkt, r + 0x53, 48000)
        struct.pack_into("<H", pkt, r + 0x55, 48000)
        return bytes(pkt)

    def _conninfo_packet(self, sender_id):
        pkt = bytearray(0x90)
        struct.pack_into("<I", pkt, 0x00, 0x90)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, sender_id)
        pkt[0x20:0x30] = bytes(range(0x01, 0x11))
        name = b"IC-7610\x00"
        pkt[0x40:0x40 + len(name)] = name
        pkt[0x60] = 0x00
        return bytes(pkt)

    def _status_response(self, sender_id):
        pkt = bytearray(0x50)
        struct.pack_into("<I", pkt, 0x00, 0x50)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, sender_id)
        struct.pack_into(">H", pkt, 0x42, self._actual_civ_port)
        struct.pack_into(">H", pkt, 0x46, self._actual_audio_port)
        return bytes(pkt)
