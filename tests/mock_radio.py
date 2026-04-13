"""Mock Icom radio UDP server for integration testing wfweb.

Emulates an IC-7610 over UDP so that wfweb can connect, authenticate,
and exchange CI-V commands without real hardware.

Two asyncio datagram servers are started:
  * control port  - authentication handshake, pings, token renewal
  * CI-V port     - CI-V command / response exchange

Adapted from morozsm/icom-lan MockIcomRadio (MIT License).
Made self-contained — no external dependencies beyond Python stdlib.

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

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------

_HEADER_SIZE = 0x10  # 16 bytes
_PING_SIZE = 0x15    # 21 bytes
_CIV_HEADER_SIZE = 0x15  # 21 bytes before CI-V frame in DATA packets

# Packet type codes
_PT_DATA = 0x00
_PT_CONTROL = 0x01
_PT_RETRANSMIT = 0x01
_PT_ARE_YOU_THERE = 0x03
_PT_I_AM_HERE = 0x04
_PT_DISCONNECT = 0x05
_PT_ARE_YOU_READY = 0x06
_PT_PING = 0x07

# CI-V addresses
_RADIO_IC7610_ADDR = 0x98   # IC-7610
_CONTROLLER_ADDR = 0xE1     # wfweb uses 0xE1 (compCivAddr)

# CI-V framing
_CIV_PREAMBLE = b"\xfe\xfe"
_CIV_TERM = b"\xfd"

# CI-V command codes
_CMD_FREQ_READ = 0x03
_CMD_MODE_READ = 0x04
_CMD_FREQ_SET = 0x05
_CMD_MODE_SET = 0x06
_CMD_LEVEL = 0x14
_CMD_METER = 0x15
_CMD_POWER_CTRL = 0x18
_CMD_TRANSCEIVER_ID = 0x19
_CMD_RX_FREQ = 0x25       # "RX Frequency" — IC-7610 periodic frequency poll
_CMD_RX_MODE = 0x26       # "RX Mode" — IC-7610 periodic mode poll
_CMD_CMD29 = 0x29
_CMD_ACK = 0xFB
_CMD_NAK = 0xFA

# Level/meter sub-commands
_SUB_AF_GAIN = 0x01
_SUB_RF_GAIN = 0x02
_SUB_SQUELCH = 0x03
_SUB_RF_POWER = 0x0A
_SUB_MIC_GAIN = 0x0B
_SUB_S_METER = 0x02
_SUB_SWR_METER = 0x12
_SUB_ALC_METER = 0x13
_SUB_POWER_METER = 0x11


# ---------------------------------------------------------------------------
# BCD helpers
# ---------------------------------------------------------------------------


def _bcd_encode_freq(freq_hz: int) -> bytes:
    """Encode frequency in Hz to Icom 5-byte BCD (little-endian digits)."""
    digits = f"{freq_hz:010d}"
    result = bytearray(5)
    for i in range(5):
        low = int(digits[9 - 2 * i])
        high = int(digits[9 - 2 * i - 1])
        result[i] = (high << 4) | low
    return bytes(result)


def _bcd_decode_freq(data: bytes) -> int:
    """Decode Icom 5-byte BCD frequency to Hz."""
    freq = 0
    for i in range(len(data)):
        high = (data[i] >> 4) & 0x0F
        low = data[i] & 0x0F
        freq += low * (10 ** (2 * i)) + high * (10 ** (2 * i + 1))
    return freq


def _level_bcd_encode(value: int) -> bytes:
    """Encode 0-9999 level to 2-byte BCD (e.g. 128 -> b'\\x01\\x28')."""
    d = f"{value:04d}"
    return bytes([(int(d[0]) << 4) | int(d[1]), (int(d[2]) << 4) | int(d[3])])


def _level_bcd_decode(data: bytes) -> int:
    """Decode 2-byte BCD level."""
    d0 = (data[0] >> 4) & 0x0F
    d1 = data[0] & 0x0F
    d2 = (data[1] >> 4) & 0x0F
    d3 = data[1] & 0x0F
    return d0 * 1000 + d1 * 100 + d2 * 10 + d3


# ---------------------------------------------------------------------------
# asyncio DatagramProtocol
# ---------------------------------------------------------------------------


class _MockProtocol(asyncio.DatagramProtocol):
    """Minimal datagram protocol that routes packets to MockIcomRadio."""

    def __init__(self, owner: MockIcomRadio, label: str) -> None:
        self._owner = owner
        self._label = label
        self._transport: asyncio.DatagramTransport | None = None

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:  # type: ignore[override]
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


class MockIcomRadio:
    """Asyncio UDP server emulating an IC-7610 for integration testing.

    Handles the full connection lifecycle on two ports:

    * **Control port** - discovery (AYT/IAH), login, token exchange, pings
    * **CI-V port**    - discovery, open/close, CI-V commands, pings

    Args:
        host: Bind address for both servers.
        radio_addr: CI-V address the mock claims to be (default 0x98 = IC-7610).
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        radio_addr: int = _RADIO_IC7610_ADDR,
    ) -> None:
        self._host = host
        self._radio_addr = radio_addr

        # UDP transports (set after start())
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

        # Radio state (for CI-V responses)
        self._frequency: int = 14_074_000  # Hz
        self._mode: int = 0x01  # USB
        self._filter: int = 1
        self._af_gain: int = 128
        self._rf_gain: int = 255
        self._rf_power: int = 100
        self._squelch: int = 0
        self._mic_gain: int = 128
        self._s_meter: int = 120
        self._swr: int = 10
        self._alc: int = 0
        self._power_meter: int = 0

        # Track received CI-V commands for test assertions
        self.civ_log: list[bytes] = []

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    async def start(self) -> None:
        """Bind all UDP servers and make the ports available."""
        loop = asyncio.get_running_loop()

        # Control port
        ctrl_transport, _ = await loop.create_datagram_endpoint(
            lambda: _MockProtocol(self, "ctrl"),
            local_addr=(self._host, 0),
        )
        self._ctrl_udp = ctrl_transport
        self._actual_ctrl_port = ctrl_transport.get_extra_info("sockname")[1]

        # CI-V port
        civ_transport, _ = await loop.create_datagram_endpoint(
            lambda: _MockProtocol(self, "civ"),
            local_addr=(self._host, 0),
        )
        self._civ_udp = civ_transport
        self._actual_civ_port = civ_transport.get_extra_info("sockname")[1]

        # Audio port (just binds — no actual audio handling needed)
        audio_transport, _ = await loop.create_datagram_endpoint(
            lambda: _MockProtocol(self, "audio"),
            local_addr=(self._host, 0),
        )
        self._audio_udp = audio_transport
        self._actual_audio_port = audio_transport.get_extra_info("sockname")[1]

        logger.info(
            "MockIcomRadio started — ctrl=%d civ=%d audio=%d",
            self._actual_ctrl_port,
            self._actual_civ_port,
            self._actual_audio_port,
        )

    async def stop(self) -> None:
        """Close all UDP servers."""
        for udp in (self._ctrl_udp, self._civ_udp, self._audio_udp):
            if udp is not None:
                udp.close()
        self._ctrl_udp = None
        self._civ_udp = None
        self._audio_udp = None
        logger.debug("MockIcomRadio stopped")

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

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
    # State setters (for test setup)
    # ------------------------------------------------------------------

    def set_frequency(self, hz: int) -> None:
        self._frequency = hz

    def set_mode(self, mode: int, filt: int = 1) -> None:
        self._mode = mode
        self._filter = filt

    def set_s_meter(self, value: int) -> None:
        self._s_meter = value

    # ------------------------------------------------------------------
    # Packet dispatch
    # ------------------------------------------------------------------

    def _on_packet(
        self,
        data: bytes,
        addr: tuple[str, int],
        label: str,
        proto: _MockProtocol,
    ) -> None:
        if len(data) < _HEADER_SIZE:
            return
        ptype = struct.unpack_from("<H", data, 4)[0]
        sender_id = struct.unpack_from("<I", data, 8)[0]
        seq = struct.unpack_from("<H", data, 6)[0]

        if label == "ctrl":
            self._ctrl_client_id = sender_id
            self._handle_ctrl(data, addr, ptype, sender_id, seq, proto)
        elif label == "civ":
            self._civ_client_id = sender_id
            self._handle_civ(data, addr, ptype, sender_id, seq, proto)
        # audio: ignore all packets (we don't need audio for tests)

    # ------------------------------------------------------------------
    # Control port
    # ------------------------------------------------------------------

    def _handle_ctrl(
        self,
        data: bytes,
        addr: tuple[str, int],
        ptype: int,
        sender_id: int,
        seq: int,
        proto: _MockProtocol,
    ) -> None:
        n = len(data)

        # Are You There -> I Am Here
        if n == _HEADER_SIZE and ptype == _PT_ARE_YOU_THERE:
            proto.send(self._ctrl_pkt(_PT_I_AM_HERE, 0, sender_id), addr)
            return

        # Are You Ready -> echo
        if n == _HEADER_SIZE and ptype == _PT_ARE_YOU_READY:
            proto.send(self._ctrl_pkt(_PT_ARE_YOU_READY, 0, sender_id), addr)
            return

        # Ping -> reply
        if n == _PING_SIZE and ptype == _PT_PING and data[0x10] == 0x00:
            proto.send(self._ping_reply(data, sender_id), addr)
            return

        # Login (0x80 bytes)
        if n == 0x80:
            proto.send(self._login_response(data, sender_id), addr)
            return

        # Token ack (0x40 bytes, requesttype=0x02) -> send capabilities + conninfo
        if n == 0x40 and data[0x15] == 0x02:
            proto.send(self._capabilities_packet(sender_id), addr)
            proto.send(self._conninfo_packet(sender_id), addr)
            return

        # Token renewal (0x40 bytes, requesttype=0x05) -> no response needed
        if n == 0x40 and data[0x15] == 0x05:
            return

        # Token remove (0x40 bytes, requesttype=0x01) -> no response
        if n == 0x40 and data[0x15] == 0x01:
            return

        # Client conninfo/stream request (0x90 bytes, requesttype=0x03) -> status with ports
        if n == 0x90 and data[0x15] == 0x03:
            proto.send(self._status_response(sender_id), addr)
            return

        # Disconnect
        if n == _HEADER_SIZE and ptype == _PT_DISCONNECT:
            return

    # ------------------------------------------------------------------
    # CI-V port
    # ------------------------------------------------------------------

    def _handle_civ(
        self,
        data: bytes,
        addr: tuple[str, int],
        ptype: int,
        sender_id: int,
        seq: int,
        proto: _MockProtocol,
    ) -> None:
        n = len(data)

        # Are You There -> I Am Here
        if n == _HEADER_SIZE and ptype == _PT_ARE_YOU_THERE:
            proto.send(self._ctrl_pkt(_PT_I_AM_HERE, 0, sender_id), addr)
            return

        # Are You Ready -> echo
        if n == _HEADER_SIZE and ptype == _PT_ARE_YOU_READY:
            proto.send(self._ctrl_pkt(_PT_ARE_YOU_READY, 0, sender_id), addr)
            return

        # Ping -> reply
        if n == _PING_SIZE and ptype == _PT_PING and data[0x10] == 0x00:
            proto.send(self._ping_reply(data, sender_id), addr)
            return

        # OpenClose (0x16 bytes)
        if n == 0x16:
            if data[0x15] == 0x04:  # open_stream
                # Send unsolicited CI-V frame to mark stream as active
                civ = self._civ_frame(to=0x00, frm=self._radio_addr, cmd=0x00)
                proto.send(self._wrap_civ(civ, sender_id), addr)
            return

        # Disconnect
        if n == _HEADER_SIZE and ptype == _PT_DISCONNECT:
            return

        # CI-V data (DATA type, larger than header)
        if ptype == _PT_DATA and n > _CIV_HEADER_SIZE:
            self._handle_civ_data(data, addr, sender_id, proto)

    def _handle_civ_data(
        self,
        data: bytes,
        addr: tuple[str, int],
        sender_id: int,
        proto: _MockProtocol,
    ) -> None:
        """Parse CI-V frame from a DATA packet and generate a response."""
        datalen = struct.unpack_from("<H", data, 0x11)[0]
        end = _CIV_HEADER_SIZE + datalen
        if end > len(data):
            return
        civ = data[_CIV_HEADER_SIZE:end]

        # Log raw CI-V for test assertions
        self.civ_log.append(civ)

        if len(civ) < 6:
            return
        if civ[:2] != _CIV_PREAMBLE:
            return
        if civ[-1:] != _CIV_TERM:
            return

        to_addr = civ[2]
        from_addr = civ[3]
        cmd = civ[4]
        payload = civ[5:-1]

        # Only handle commands addressed to this radio
        if to_addr != self._radio_addr:
            return

        response_civ = self._dispatch_civ(cmd, payload, from_addr)
        if response_civ is not None:
            proto.send(self._wrap_civ(response_civ, sender_id), addr)

    # ------------------------------------------------------------------
    # CI-V command dispatcher
    # ------------------------------------------------------------------

    def _dispatch_civ(self, cmd: int, payload: bytes, from_addr: int) -> bytes | None:
        to = from_addr
        frm = self._radio_addr

        # --- Command29 wrapper (IC-7610 dual-receiver commands) ---
        if cmd == _CMD_CMD29:
            return self._handle_cmd29(to, frm, payload)

        # --- Transceiver ID (0x19) ---
        if cmd == _CMD_TRANSCEIVER_ID:
            # Respond with model byte
            return self._civ_frame(to, frm, _CMD_TRANSCEIVER_ID, data=bytes([0x00, self._radio_addr]))

        # --- Frequency read (0x03) ---
        if cmd == _CMD_FREQ_READ:
            return self._civ_frame(to, frm, _CMD_FREQ_READ, data=_bcd_encode_freq(self._frequency))

        # --- Frequency set (0x05) ---
        if cmd == _CMD_FREQ_SET:
            if len(payload) == 5:
                self._frequency = _bcd_decode_freq(payload)
            return self._civ_ack(to, frm)

        # --- Mode read (0x04) ---
        if cmd == _CMD_MODE_READ:
            return self._civ_frame(to, frm, _CMD_MODE_READ, data=bytes([self._mode, self._filter]))

        # --- Mode set (0x06) ---
        if cmd == _CMD_MODE_SET:
            if payload:
                self._mode = payload[0]
                if len(payload) > 1:
                    self._filter = payload[1]
            return self._civ_ack(to, frm)

        # --- Level (0x14) ---
        if cmd == _CMD_LEVEL:
            if not payload:
                return self._civ_ack(to, frm)
            sub = payload[0]
            rest = payload[1:]
            return self._handle_level(to, frm, sub, rest)

        # --- Meter (0x15) ---
        if cmd == _CMD_METER:
            if not payload:
                return self._civ_ack(to, frm)
            sub = payload[0]
            return self._handle_meter(to, frm, sub)

        # --- Power control (0x18) ---
        if cmd == _CMD_POWER_CTRL:
            # Power on/off - just ACK
            return self._civ_ack(to, frm)

        # --- RX Frequency (0x25) — IC-7610 periodic frequency poll ---
        # GET: 25 <receiver> FD → response: 25 <receiver> <5 BCD> FD
        # SET: 25 <receiver> <5 BCD> FD → ACK
        if cmd == _CMD_RX_FREQ:
            receiver = payload[0] if payload else 0
            rest = payload[1:]
            if rest:  # SET
                if len(rest) == 5:
                    self._frequency = _bcd_decode_freq(rest)
                return self._civ_ack(to, frm)
            else:  # GET
                freq_data = _bcd_encode_freq(self._frequency)
                return self._civ_frame(to, frm, _CMD_RX_FREQ, data=bytes([receiver]) + freq_data)

        # --- RX Mode (0x26) — IC-7610 periodic mode poll ---
        # GET: 26 <receiver> FD → response: 26 <receiver> <mode> <filter> FD
        # SET: 26 <receiver> <mode> [<filter>] FD → ACK
        if cmd == _CMD_RX_MODE:
            receiver = payload[0] if payload else 0
            rest = payload[1:]
            if rest:  # SET
                self._mode = rest[0]
                if len(rest) > 1:
                    self._filter = rest[1]
                return self._civ_ack(to, frm)
            else:  # GET
                return self._civ_frame(to, frm, _CMD_RX_MODE, data=bytes([receiver, self._mode, self._filter]))

        # ACK/NAK from client - ignore
        if cmd in (_CMD_ACK, _CMD_NAK):
            return None

        # Unknown -> ACK (prevent wfweb "radio powered off" detection)
        return self._civ_ack(to, frm)

    def _handle_cmd29(self, to: int, frm: int, payload: bytes) -> bytes | None:
        """Handle Command29 (0x29) — IC-7610 dual-receiver command wrapper.

        Format: 29 <receiver> <subcmd> [data...] FD
        The receiver byte and subcmd are in the payload (after 0x29 is stripped).
        """
        if len(payload) < 2:
            return self._civ_ack(to, frm)

        receiver = payload[0]
        subcmd = payload[1]
        rest = payload[2:]

        # Sub-command 0x00 = Frequency
        # wfweb appends receiver byte to freq/mode payloads (icomcommander.cpp:2917)
        # so responses must include: 29 <receiver> <subcmd> <receiver> <data> FD
        if subcmd == 0x00:
            if rest:  # SET: rest = receiver + 5-byte BCD frequency
                bcd = rest[1:] if len(rest) == 6 else rest
                if len(bcd) == 5:
                    self._frequency = _bcd_decode_freq(bcd)
                return self._civ_ack(to, frm)
            else:  # GET
                freq_data = _bcd_encode_freq(self._frequency)
                return self._civ_frame(
                    to, frm, _CMD_CMD29,
                    data=bytes([receiver, subcmd, receiver]) + freq_data,
                )

        # Sub-command 0x01 = Mode
        if subcmd == 0x01:
            if rest:  # SET: rest = receiver + mode [+ filter]
                mode_data = rest[1:] if len(rest) >= 2 else rest
                if mode_data:
                    self._mode = mode_data[0]
                if len(mode_data) > 1:
                    self._filter = mode_data[1]
                return self._civ_ack(to, frm)
            else:  # GET
                return self._civ_frame(
                    to, frm, _CMD_CMD29,
                    data=bytes([receiver, subcmd, receiver, self._mode, self._filter]),
                )

        # Unknown sub-command -> ACK to avoid "radio powered off" detection
        return self._civ_ack(to, frm)

    def _handle_level(self, to: int, frm: int, sub: int, rest: bytes) -> bytes | None:
        levels = {
            _SUB_AF_GAIN: ("_af_gain", self._af_gain),
            _SUB_RF_GAIN: ("_rf_gain", self._rf_gain),
            _SUB_SQUELCH: ("_squelch", self._squelch),
            _SUB_RF_POWER: ("_rf_power", self._rf_power),
            _SUB_MIC_GAIN: ("_mic_gain", self._mic_gain),
        }
        if sub not in levels:
            return self._civ_ack(to, frm)

        attr, current = levels[sub]
        if rest:  # SET
            setattr(self, attr, _level_bcd_decode(rest))
            return self._civ_ack(to, frm)
        else:  # GET
            return self._civ_frame(to, frm, _CMD_LEVEL, sub=sub, data=_level_bcd_encode(current))

    def _handle_meter(self, to: int, frm: int, sub: int) -> bytes | None:
        meters = {
            _SUB_S_METER: self._s_meter,
            _SUB_SWR_METER: self._swr,
            _SUB_ALC_METER: self._alc,
            _SUB_POWER_METER: self._power_meter,
        }
        if sub not in meters:
            return self._civ_ack(to, frm)
        return self._civ_frame(to, frm, _CMD_METER, sub=sub, data=_level_bcd_encode(meters[sub]))

    # ------------------------------------------------------------------
    # CI-V frame builders
    # ------------------------------------------------------------------

    def _civ_frame(
        self,
        to: int,
        frm: int,
        cmd: int,
        sub: int | None = None,
        data: bytes | None = None,
    ) -> bytes:
        frame = bytearray(_CIV_PREAMBLE)
        frame.append(to)
        frame.append(frm)
        frame.append(cmd)
        if sub is not None:
            frame.append(sub)
        if data:
            frame.extend(data)
        frame.extend(_CIV_TERM)
        return bytes(frame)

    def _civ_ack(self, to: int, frm: int) -> bytes:
        return self._civ_frame(to, frm, _CMD_ACK)

    def _civ_nak(self, to: int, frm: int) -> bytes:
        return self._civ_frame(to, frm, _CMD_NAK)

    def _wrap_civ(self, civ_frame: bytes, client_id: int) -> bytes:
        """Wrap a CI-V frame in a DATA header for the CI-V port."""
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

    # ------------------------------------------------------------------
    # Control packet builders
    # ------------------------------------------------------------------

    def _ctrl_pkt(self, ptype: int, seq: int, client_id: int) -> bytes:
        pkt = bytearray(_HEADER_SIZE)
        struct.pack_into("<I", pkt, 0x00, _HEADER_SIZE)
        struct.pack_into("<H", pkt, 0x04, ptype)
        struct.pack_into("<H", pkt, 0x06, seq)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, client_id)
        return bytes(pkt)

    def _ping_reply(self, data: bytes, client_id: int) -> bytes:
        pkt = bytearray(_PING_SIZE)
        struct.pack_into("<I", pkt, 0x00, _PING_SIZE)
        struct.pack_into("<H", pkt, 0x04, _PT_PING)
        seq = struct.unpack_from("<H", data, 6)[0]
        struct.pack_into("<H", pkt, 0x06, seq)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, client_id)
        pkt[0x10] = 0x01  # reply flag
        pkt[0x11:0x15] = data[0x11:0x15]  # echo timestamp
        return bytes(pkt)

    def _login_response(self, data: bytes, sender_id: int) -> bytes:
        tok_request = struct.unpack_from("<H", data, 0x1A)[0]
        pkt = bytearray(0x60)
        struct.pack_into("<I", pkt, 0x00, 0x60)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, sender_id)
        struct.pack_into("<H", pkt, 0x1A, tok_request)
        struct.pack_into("<I", pkt, 0x1C, self.token)
        # error at 0x30 stays 0x00000000 = success
        pkt[0x40:0x44] = b"FTTH"
        return bytes(pkt)

    def _capabilities_packet(self, sender_id: int) -> bytes:
        """Build a capabilities header (0x42) + one radio_cap record (0x66) = 0xA8 bytes."""
        _CAP_HEADER = 0x42
        _RADIO_CAP = 0x66
        total = _CAP_HEADER + _RADIO_CAP
        pkt = bytearray(total)

        # Header
        struct.pack_into("<I", pkt, 0x00, total)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, sender_id)
        struct.pack_into("<H", pkt, 0x40, 1)  # numradios = 1

        # Radio capability record starts at offset 0x42
        r = _CAP_HEADER
        # GUID at radio offset 0x00 (16 bytes)
        pkt[r:r + 16] = bytes(range(0x01, 0x11))
        # name at radio offset 0x10 (32 bytes)
        name = b"IC-7610\x00"
        pkt[r + 0x10:r + 0x10 + len(name)] = name
        # audio at radio offset 0x30 (32 bytes)
        audio = b"IC-7610 USB Audio\x00"
        pkt[r + 0x30:r + 0x30 + len(audio)] = audio
        # civ at radio offset 0x52
        pkt[r + 0x52] = self._radio_addr
        # rxsample at radio offset 0x53 (LE)
        struct.pack_into("<H", pkt, r + 0x53, 48000)
        # txsample at radio offset 0x55 (LE)
        struct.pack_into("<H", pkt, r + 0x55, 48000)

        return bytes(pkt)

    def _conninfo_packet(self, sender_id: int) -> bytes:
        """Build a CONNINFO (0x90) packet for one radio (busy=0)."""
        pkt = bytearray(0x90)
        struct.pack_into("<I", pkt, 0x00, 0x90)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, sender_id)
        # GUID at 0x20 (must match the capabilities packet)
        pkt[0x20:0x30] = bytes(range(0x01, 0x11))
        # name at 0x40 (use same as capabilities)
        name = b"IC-7610\x00"
        pkt[0x40:0x40 + len(name)] = name
        # busy at 0x60 = 0 (not busy)
        pkt[0x60] = 0x00
        return bytes(pkt)

    def _status_response(self, sender_id: int) -> bytes:
        """Build status packet containing CI-V and audio port numbers."""
        pkt = bytearray(0x50)
        struct.pack_into("<I", pkt, 0x00, 0x50)
        struct.pack_into("<H", pkt, 0x04, _PT_DATA)
        struct.pack_into("<I", pkt, 0x08, self.radio_id)
        struct.pack_into("<I", pkt, 0x0C, sender_id)
        # CI-V port at offset 0x42 (big-endian)
        struct.pack_into(">H", pkt, 0x42, self._actual_civ_port)
        # Audio port at offset 0x46 (big-endian)
        struct.pack_into(">H", pkt, 0x46, self._actual_audio_port)
        return bytes(pkt)
