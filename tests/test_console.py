"""Terminal status page (src/consolestatus.cpp), end to end.

wfweb is started on a pseudo-terminal so it believes it is interactive, and
the bytes it writes are inspected: the page must appear, follow a resize,
switch to the live log and back, and always hand the terminal back as it found
it.  The same binary on a pipe, with --no-tui, WFWEB_NO_TUI or -b must print
the plain log with no escape sequences at all.

No rig is involved: the server starts with --lan 127.0.0.1 --no-autoconnect.
"""

from __future__ import annotations

import os
import re
import select
import shutil
import signal
import subprocess
import sys
import time

import pytest

from conftest import WFWEB_BIN, _find_free_port

if sys.platform == "win32":
    pytest.skip("pseudo-terminals are POSIX only", allow_module_level=True)

import fcntl
import pty
import struct
import termios

PAGE_ON = b"\x1b[?1049h"
PAGE_OFF = b"\x1b[?1049l"
LOG_LINE = re.compile(rb"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} \S")
LFLAG = 3


class Terminal:
    """wfweb running on a pseudo-terminal of a given size."""

    def __init__(self, tmp_path, *extra_args, rows=24, cols=80, env=None):
        if not WFWEB_BIN.exists():
            pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")
        self.port = _find_free_port()
        self.log = tmp_path / "wfweb.log"
        self.master, self.slave = pty.openpty()
        self.resize(rows, cols)
        self.before = termios.tcgetattr(self.slave)
        environ = dict(os.environ, HOME=str(tmp_path), TERM="xterm")
        environ.pop("WFWEB_NO_TUI", None)
        environ.update(env or {})
        self.proc = subprocess.Popen(
            [str(WFWEB_BIN), "-p", str(self.port), "--lan", "127.0.0.1",
             "--no-autoconnect", "-l", str(self.log), *extra_args],
            stdin=self.slave, stdout=self.slave, stderr=self.slave,
            env=environ, start_new_session=True,
        )
        self.output = b""

    def resize(self, rows, cols):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def read(self, seconds):
        """Collect whatever is written during the next `seconds`."""
        chunk = b""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], 0.1)[0]:
                try:
                    chunk += os.read(self.master, 65536)
                except OSError:
                    break
        self.output += chunk
        return chunk

    def wait_for(self, needle, timeout=15.0):
        """Read until `needle` (bytes or compiled regex) shows up in new output."""
        seen = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            seen += self.read(0.2)
            found = needle.search(seen) if hasattr(needle, "search") else needle in seen
            if found:
                return seen
        pytest.fail(f"{needle!r} not seen within {timeout}s; got {seen[-400:]!r}")

    def type(self, keys):
        os.write(self.master, keys)

    def wait_exit(self, timeout=10.0):
        deadline = time.monotonic() + timeout
        while self.proc.poll() is None and time.monotonic() < deadline:
            self.read(0.1)
        if self.proc.poll() is None:
            pytest.fail("wfweb did not exit")
        self.read(0.3)
        return self.proc.returncode

    def restored(self):
        """The terminal line settings are back to what they were before."""
        return termios.tcgetattr(self.slave)[LFLAG] == self.before[LFLAG]

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=5)
        os.close(self.master)
        os.close(self.slave)


@pytest.fixture
def terminal(tmp_path):
    made = []

    def make(*args, **kwargs):
        made.append(Terminal(tmp_path, *args, **kwargs))
        return made[-1]

    yield make
    for term in made:
        term.close()


def painted_rows(frame):
    """Row numbers addressed by the page's cursor-position sequences."""
    return [int(n) for n in re.findall(rb"\x1b\[(\d+);1H", frame)]


def test_page_shows_url_and_rig(terminal):
    term = terminal()
    url = re.compile(rb"https?://[^\s\x1b]+:%d/" % term.port)
    seen = term.wait_for(url)
    assert PAGE_ON in term.output
    seen += term.read(1.5)
    assert b"Open in a browser:" in seen
    assert b"LAN 127.0.0.1  waiting for the rig" in seen
    assert str(term.log).encode() in seen
    assert b"[l] live log   [q] quit" in seen
    # While the page is up the log goes to the file only.
    assert not LOG_LINE.search(term.output)
    assert LOG_LINE.search(term.log.read_bytes())


def test_page_follows_resize(terminal):
    term = terminal(rows=24, cols=80)
    term.wait_for(b"[l] live log")
    assert max(painted_rows(term.output)) > 5

    term.resize(3, 30)
    term.read(1.5)                       # let a frame in the old size drain
    frame = term.read(2.5)
    assert frame, "the page keeps repainting"
    assert max(painted_rows(frame)) <= 3
    for text in re.findall(rb"\x1b\[\d+;1H([^\x1b]*)", frame):
        assert len(text) <= 30
    assert b"http" in frame              # the URL is still on screen


def test_l_toggles_live_log(terminal):
    term = terminal()
    term.wait_for(b"[l] live log")

    term.type(b"l")
    seen = term.wait_for(b"--- live log")
    assert PAGE_OFF in seen
    assert LOG_LINE.search(seen), "the backlog is replayed"

    term.type(b"l")
    term.wait_for(PAGE_ON)
    term.wait_for(b"[l] live log")


def test_debug_starts_in_log_view(terminal):
    term = terminal("-d")
    seen = term.wait_for(LOG_LINE)
    assert b"--- live log" in seen
    assert PAGE_ON not in term.output


@pytest.mark.parametrize("when", ["starting", "running"])
def test_q_quits_and_restores_terminal(terminal, when):
    term = terminal()
    # The first frame is painted before the server exists; a key typed that
    # early must not be lost.
    term.wait_for(b"[l] live log" if when == "starting" else b"Open in a browser:")
    assert not term.restored(), "single-key mode while the page is up"

    term.type(b"q")
    assert term.wait_exit() == 0
    assert PAGE_OFF in term.output
    assert term.output.rstrip().endswith(b"wfweb stopped. Log: " + str(term.log).encode())
    assert term.restored()


@pytest.mark.parametrize("sig", [signal.SIGINT, signal.SIGTERM, signal.SIGHUP])
def test_signal_restores_terminal(terminal, sig):
    term = terminal()
    term.wait_for(b"[l] live log")
    term.proc.send_signal(sig)
    term.wait_exit()
    assert PAGE_OFF in term.output
    assert term.output.rfind(PAGE_OFF) > term.output.rfind(PAGE_ON)
    assert term.restored()


@pytest.mark.parametrize("args,env", [
    (["--no-tui"], {}),
    ([], {"WFWEB_NO_TUI": "1"}),
    ([], {"TERM": "dumb"}),
])
def test_plain_log_on_request(terminal, args, env):
    term = terminal(*args, env=env)
    term.wait_for(LOG_LINE)
    term.read(2.0)
    assert b"\x1b" not in term.output
    assert term.restored(), "terminal never touched"
    term.proc.send_signal(signal.SIGTERM)
    term.wait_exit()
    assert b"\x1b" not in term.output
    assert b"wfweb stopped" not in term.output


def test_plain_log_on_a_pipe(tmp_path):
    if not WFWEB_BIN.exists():
        pytest.skip(f"wfweb binary not found at {WFWEB_BIN}")
    proc = subprocess.Popen(
        [str(WFWEB_BIN), "-p", str(_find_free_port()), "--lan", "127.0.0.1",
         "--no-autoconnect", "-l", str(tmp_path / "wfweb.log")],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=dict(os.environ, HOME=str(tmp_path), TERM="xterm"),
    )
    time.sleep(3)
    proc.terminate()
    out, _ = proc.communicate(timeout=10)
    assert LOG_LINE.search(out)
    assert b"\x1b" not in out
    assert b"wfweb stopped" not in out


def test_daemon_leaves_terminal_alone(terminal):
    if not shutil.which("pkill"):
        pytest.skip("pkill is needed to stop the daemon afterwards")
    term = terminal("-b")
    try:
        assert term.wait_exit() == 0     # the parent returns at once
        term.read(2.0)
        assert b"\x1b" not in term.output
        assert term.restored()
    finally:
        subprocess.run(["pkill", "-f", f"{WFWEB_BIN} -p {term.port} "], check=False)
