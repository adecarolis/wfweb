"""Shared CI-V command dispatch logic for mock radios (LAN and serial).

This module contains:
  - Protocol constants and CI-V command codes
  - BCD encoding/decoding helpers
  - CivRadioBase: a base class with radio state and CI-V command handling

Both MockIcomRadio (UDP/LAN) and SerialMockRadio (PTY/USB) inherit from
CivRadioBase to share the CI-V logic while using different transports.
"""

from __future__ import annotations

# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------

RADIO_IC7610_ADDR = 0x98
CONTROLLER_ADDR = 0xE1

CIV_PREAMBLE = b"\xfe\xfe"
CIV_TERM = b"\xfd"

# CI-V command codes
CMD_FREQ_READ = 0x03
CMD_MODE_READ = 0x04
CMD_FREQ_SET = 0x05
CMD_MODE_SET = 0x06
CMD_LEVEL = 0x14
CMD_METER = 0x15
CMD_POWER_CTRL = 0x18
CMD_TRANSCEIVER_ID = 0x19
CMD_RX_FREQ = 0x25
CMD_RX_MODE = 0x26
CMD_CMD29 = 0x29
CMD_ACK = 0xFB
CMD_NAK = 0xFA

# Level/meter sub-commands
SUB_AF_GAIN = 0x01
SUB_RF_GAIN = 0x02
SUB_SQUELCH = 0x03
SUB_RF_POWER = 0x0A
SUB_MIC_GAIN = 0x0B
SUB_S_METER = 0x02
SUB_SWR_METER = 0x12
SUB_ALC_METER = 0x13
SUB_POWER_METER = 0x11


# ---------------------------------------------------------------------------
# BCD helpers
# ---------------------------------------------------------------------------


def bcd_encode_freq(freq_hz: int) -> bytes:
    """Encode frequency in Hz to Icom 5-byte BCD (little-endian digits)."""
    digits = f"{freq_hz:010d}"
    result = bytearray(5)
    for i in range(5):
        low = int(digits[9 - 2 * i])
        high = int(digits[9 - 2 * i - 1])
        result[i] = (high << 4) | low
    return bytes(result)


def bcd_decode_freq(data: bytes) -> int:
    """Decode Icom 5-byte BCD frequency to Hz."""
    freq = 0
    for i in range(len(data)):
        high = (data[i] >> 4) & 0x0F
        low = data[i] & 0x0F
        freq += low * (10 ** (2 * i)) + high * (10 ** (2 * i + 1))
    return freq


def level_bcd_encode(value: int) -> bytes:
    """Encode 0-9999 level to 2-byte BCD."""
    d = f"{value:04d}"
    return bytes([(int(d[0]) << 4) | int(d[1]), (int(d[2]) << 4) | int(d[3])])


def level_bcd_decode(data: bytes) -> int:
    """Decode 2-byte BCD level."""
    d0 = (data[0] >> 4) & 0x0F
    d1 = data[0] & 0x0F
    d2 = (data[1] >> 4) & 0x0F
    d3 = data[1] & 0x0F
    return d0 * 1000 + d1 * 100 + d2 * 10 + d3


# ---------------------------------------------------------------------------
# CI-V Radio Base
# ---------------------------------------------------------------------------


class CivRadioBase:
    """Base class with radio state and CI-V command dispatch.

    Subclasses provide the transport (UDP for LAN, PTY for USB).
    """

    def __init__(self, radio_addr: int = RADIO_IC7610_ADDR) -> None:
        self._radio_addr = radio_addr

        # Radio state
        self._frequency: int = 14_074_000
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
    # CI-V frame builders
    # ------------------------------------------------------------------

    def civ_frame(
        self,
        to: int,
        frm: int,
        cmd: int,
        sub: int | None = None,
        data: bytes | None = None,
    ) -> bytes:
        frame = bytearray(CIV_PREAMBLE)
        frame.append(to)
        frame.append(frm)
        frame.append(cmd)
        if sub is not None:
            frame.append(sub)
        if data:
            frame.extend(data)
        frame.extend(CIV_TERM)
        return bytes(frame)

    def civ_ack(self, to: int, frm: int) -> bytes:
        return self.civ_frame(to, frm, CMD_ACK)

    def civ_nak(self, to: int, frm: int) -> bytes:
        return self.civ_frame(to, frm, CMD_NAK)

    # ------------------------------------------------------------------
    # CI-V command dispatcher
    # ------------------------------------------------------------------

    def dispatch_civ(self, cmd: int, payload: bytes, from_addr: int) -> bytes | None:
        to = from_addr
        frm = self._radio_addr

        if cmd == CMD_CMD29:
            return self._handle_cmd29(to, frm, payload)

        if cmd == CMD_TRANSCEIVER_ID:
            return self.civ_frame(to, frm, CMD_TRANSCEIVER_ID,
                                  data=bytes([0x00, self._radio_addr]))

        if cmd == CMD_FREQ_READ:
            return self.civ_frame(to, frm, CMD_FREQ_READ,
                                  data=bcd_encode_freq(self._frequency))

        if cmd == CMD_FREQ_SET:
            if len(payload) == 5:
                self._frequency = bcd_decode_freq(payload)
            return self.civ_ack(to, frm)

        if cmd == CMD_MODE_READ:
            return self.civ_frame(to, frm, CMD_MODE_READ,
                                  data=bytes([self._mode, self._filter]))

        if cmd == CMD_MODE_SET:
            if payload:
                self._mode = payload[0]
                if len(payload) > 1:
                    self._filter = payload[1]
            return self.civ_ack(to, frm)

        if cmd == CMD_LEVEL:
            if not payload:
                return self.civ_ack(to, frm)
            return self._handle_level(to, frm, payload[0], payload[1:])

        if cmd == CMD_METER:
            if not payload:
                return self.civ_ack(to, frm)
            return self._handle_meter(to, frm, payload[0])

        if cmd == CMD_POWER_CTRL:
            return self.civ_ack(to, frm)

        if cmd == CMD_RX_FREQ:
            receiver = payload[0] if payload else 0
            rest = payload[1:]
            if rest:
                if len(rest) == 5:
                    self._frequency = bcd_decode_freq(rest)
                return self.civ_ack(to, frm)
            else:
                return self.civ_frame(to, frm, CMD_RX_FREQ,
                                      data=bytes([receiver]) + bcd_encode_freq(self._frequency))

        if cmd == CMD_RX_MODE:
            receiver = payload[0] if payload else 0
            rest = payload[1:]
            if rest:
                self._mode = rest[0]
                if len(rest) > 1:
                    self._filter = rest[1]
                return self.civ_ack(to, frm)
            else:
                return self.civ_frame(to, frm, CMD_RX_MODE,
                                      data=bytes([receiver, self._mode, self._filter]))

        if cmd in (CMD_ACK, CMD_NAK):
            return None

        return self.civ_ack(to, frm)

    def _handle_cmd29(self, to: int, frm: int, payload: bytes) -> bytes | None:
        if len(payload) < 2:
            return self.civ_ack(to, frm)

        receiver = payload[0]
        subcmd = payload[1]
        rest = payload[2:]

        if subcmd == 0x00:  # Frequency
            if rest:
                bcd = rest[1:] if len(rest) == 6 else rest
                if len(bcd) == 5:
                    self._frequency = bcd_decode_freq(bcd)
                return self.civ_ack(to, frm)
            else:
                return self.civ_frame(
                    to, frm, CMD_CMD29,
                    data=bytes([receiver, subcmd, receiver]) + bcd_encode_freq(self._frequency),
                )

        if subcmd == 0x01:  # Mode
            if rest:
                mode_data = rest[1:] if len(rest) >= 2 else rest
                if mode_data:
                    self._mode = mode_data[0]
                if len(mode_data) > 1:
                    self._filter = mode_data[1]
                return self.civ_ack(to, frm)
            else:
                return self.civ_frame(
                    to, frm, CMD_CMD29,
                    data=bytes([receiver, subcmd, receiver, self._mode, self._filter]),
                )

        return self.civ_ack(to, frm)

    def _handle_level(self, to: int, frm: int, sub: int, rest: bytes) -> bytes | None:
        levels = {
            SUB_AF_GAIN: ("_af_gain", self._af_gain),
            SUB_RF_GAIN: ("_rf_gain", self._rf_gain),
            SUB_SQUELCH: ("_squelch", self._squelch),
            SUB_RF_POWER: ("_rf_power", self._rf_power),
            SUB_MIC_GAIN: ("_mic_gain", self._mic_gain),
        }
        if sub not in levels:
            return self.civ_ack(to, frm)

        attr, current = levels[sub]
        if rest:
            setattr(self, attr, level_bcd_decode(rest))
            return self.civ_ack(to, frm)
        else:
            return self.civ_frame(to, frm, CMD_LEVEL, sub=sub,
                                  data=level_bcd_encode(current))

    def _handle_meter(self, to: int, frm: int, sub: int) -> bytes | None:
        meters = {
            SUB_S_METER: self._s_meter,
            SUB_SWR_METER: self._swr,
            SUB_ALC_METER: self._alc,
            SUB_POWER_METER: self._power_meter,
        }
        if sub not in meters:
            return self.civ_ack(to, frm)
        return self.civ_frame(to, frm, CMD_METER, sub=sub,
                              data=level_bcd_encode(meters[sub]))

    # ------------------------------------------------------------------
    # CI-V frame parser (extracts command from raw frame bytes)
    # ------------------------------------------------------------------

    def parse_and_dispatch(self, frame: bytes) -> bytes | None:
        """Parse a raw CI-V frame and return the response frame (or None)."""
        self.civ_log.append(frame)

        if len(frame) < 6:
            return None
        if frame[:2] != CIV_PREAMBLE or frame[-1:] != CIV_TERM:
            return None

        to_addr = frame[2]
        from_addr = frame[3]
        cmd = frame[4]
        payload = frame[5:-1]

        if to_addr != self._radio_addr:
            return None

        return self.dispatch_civ(cmd, payload, from_addr)
