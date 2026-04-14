"""Mock Icom radio over PTY (virtual serial port) for USB integration tests.

Creates a PTY pair: wfweb connects to the slave end via QSerialPort,
the mock reads/writes raw CI-V frames on the master end.

Usage::

    mock = SerialMockRadio()
    mock.start()
    # wfweb --serial-port <mock.port_path> --civ 0x98
    mock.stop()
"""

from __future__ import annotations

import logging
import os
import pty
import select
import termios
import threading

from mock_civ import CivRadioBase, CIV_PREAMBLE, CIV_TERM, RADIO_IC7610_ADDR

logger = logging.getLogger(__name__)


class SerialMockRadio(CivRadioBase):
    """PTY-based mock radio for testing wfweb's USB/serial code path.

    Inherits CI-V command handling from CivRadioBase. This class provides
    the serial transport over a Linux PTY pair.
    """

    def __init__(self, radio_addr: int = RADIO_IC7610_ADDR) -> None:
        super().__init__(radio_addr)
        self._master_fd: int | None = None
        self._slave_fd: int | None = None
        self._slave_path: str = ""
        self._thread: threading.Thread | None = None
        self._running = False

    @property
    def port_path(self) -> str:
        """The PTY slave path for wfweb to open (e.g. /dev/pts/42)."""
        return self._slave_path

    def start(self) -> None:
        """Create the PTY pair and start the reader thread."""
        master_fd, slave_fd = pty.openpty()
        self._master_fd = master_fd
        self._slave_fd = slave_fd
        self._slave_path = os.ttyname(slave_fd)

        # Configure the PTY for raw mode (no echo, no line buffering)
        attrs = termios.tcgetattr(master_fd)
        attrs[3] &= ~(termios.ECHO | termios.ICANON)  # lflags
        attrs[6][termios.VMIN] = 1
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(master_fd, termios.TCSANOW, attrs)

        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

        logger.info("SerialMockRadio started on %s", self._slave_path)

    def stop(self) -> None:
        """Stop the reader thread and close the PTY."""
        self._running = False
        # Close master to unblock the reader thread
        if self._master_fd is not None:
            try:
                os.close(self._master_fd)
            except OSError:
                pass
            self._master_fd = None
        if self._slave_fd is not None:
            try:
                os.close(self._slave_fd)
            except OSError:
                pass
            self._slave_fd = None
        if self._thread is not None:
            self._thread.join(timeout=3)
            self._thread = None

    def _run(self) -> None:
        """Reader thread: read CI-V frames from PTY and send responses."""
        buf = bytearray()
        fd = self._master_fd  # snapshot the fd; stop() sets it to None

        while self._running and fd is not None:
            try:
                # Wait for data with timeout so we can check _running
                ready, _, _ = select.select([fd], [], [], 0.1)
                if not ready:
                    continue

                data = os.read(fd, 4096)
                if not data:
                    break

                buf.extend(data)

                # Extract complete CI-V frames from the buffer
                while True:
                    start = buf.find(CIV_PREAMBLE)
                    if start < 0:
                        buf.clear()
                        break

                    # Discard any garbage before the preamble
                    if start > 0:
                        buf = buf[start:]

                    end = buf.find(CIV_TERM, 2)
                    if end < 0:
                        # Incomplete frame, wait for more data
                        break

                    frame = bytes(buf[:end + 1])
                    buf = buf[end + 1:]

                    response = self.parse_and_dispatch(frame)
                    if response is not None:
                        self._write(response)

            except OSError:
                break

    def _write(self, data: bytes) -> None:
        """Write data to the PTY master fd."""
        if self._master_fd is not None:
            try:
                os.write(self._master_fd, data)
            except OSError:
                pass
