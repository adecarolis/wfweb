"""Mock Yaesu radio (CAT over a PTY) for the serial code path of the Yaesu
commander, answering as an FT-710.

Yaesu CAT is text: a command is letters, then its value, then ';'. A read is
the bare command (``FA;``) and the rig answers with the value (``FA014074000;``);
a set carries the value and gets no answer. The mock answers the reads wfweb
needs to identify and run the rig (ID, PS, FA, MD0, AI) and keeps the system
date and time (DT0 / DT1) that the rigctld clock commands use. Anything else is
logged and not answered, which wfweb treats as a read the rig does not support.

Usage::

    mock = YaesuMockRadio()
    mock.start()
    # wfweb --serial-port <mock.port_path> --manufacturer 2
    mock.stop()
"""

from __future__ import annotations

import os
import pty
import re
import select
import termios
import threading

FT710_ID = "0800"


class YaesuMockRadio:
    def __init__(self, model_id: str = FT710_ID) -> None:
        self.model_id = model_id
        self.frequency = 14_074_000
        self.mode = "2"            # MD0: 2 = USB
        self.date = "20240102"     # DT0: YYYYMMDD
        self.time = "030405"       # DT1: HHMMSS
        self.cat_log: list[str] = []   # every command received, without ';'
        self._master_fd: int | None = None
        self._slave_fd: int | None = None
        self._slave_path = ""
        self._thread: threading.Thread | None = None
        self._running = False

    @property
    def port_path(self) -> str:
        return self._slave_path

    def start(self) -> None:
        master_fd, slave_fd = pty.openpty()
        self._master_fd, self._slave_fd = master_fd, slave_fd
        self._slave_path = os.ttyname(slave_fd)
        attrs = termios.tcgetattr(master_fd)
        attrs[3] &= ~(termios.ECHO | termios.ICANON)
        attrs[6][termios.VMIN] = 1
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(master_fd, termios.TCSANOW, attrs)
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        for name in ("_master_fd", "_slave_fd"):
            fd = getattr(self, name)
            if fd is not None:
                try:
                    os.close(fd)
                except OSError:
                    pass
                setattr(self, name, None)
        if self._thread is not None:
            self._thread.join(timeout=3)
            self._thread = None

    def answer(self, cmd: str) -> str | None:
        """The rig's reply to one command (without its ';'), or None."""
        if cmd == "ID":
            return f"ID{self.model_id}"
        if cmd == "PS":
            return "PS1"
        if cmd == "AI":
            return "AI0"
        if cmd == "FA":
            return f"FA{self.frequency:09d}"
        if m := re.fullmatch(r"FA(\d{9})", cmd):
            self.frequency = int(m.group(1))
            return None
        if cmd == "MD0":
            return f"MD0{self.mode}"
        if m := re.fullmatch(r"MD0([0-9A-F])", cmd):
            self.mode = m.group(1)
            return None
        if cmd == "DT0":
            return f"DT0{self.date}"
        if m := re.fullmatch(r"DT0(\d{8})", cmd):
            self.date = m.group(1)
            return None
        if cmd == "DT1":
            return f"DT1{self.time}"
        if m := re.fullmatch(r"DT1(\d{6})", cmd):
            self.time = m.group(1)
            return None
        return None

    def _run(self) -> None:
        buf = b""
        fd = self._master_fd
        while self._running and fd is not None:
            try:
                ready, _, _ = select.select([fd], [], [], 0.1)
                if not ready:
                    continue
                data = os.read(fd, 4096)
                if not data:
                    break
                buf += data
                while b";" in buf:
                    raw, buf = buf.split(b";", 1)
                    cmd = raw.decode("ascii", "replace").strip()
                    if not cmd:
                        continue
                    self.cat_log.append(cmd)
                    reply = self.answer(cmd)
                    if reply is not None:
                        os.write(fd, (reply + ";").encode("ascii"))
            except OSError:
                break
