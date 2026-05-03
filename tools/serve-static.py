#!/usr/bin/env python3
"""
Static file server for the wfweb dist that sends Cache-Control: no-store
on every response. python3 -m http.server (the default) caches aggressively
on the browser side, which makes iterative testing painful — every JS edit
needs a hard refresh, and a forgotten one looks like "the change didn't
work" when the dist on disk is fine.

The directory served is fixed: <repo>/dist, resolved from this script's
location. There is no directory argument — it has been a recurring source
of bugs where stale or stray paths (e.g. /tmp/wfweb-dist) got served
instead of the dist the user just rebuilt. Run tools/build-static.sh first
if dist/ is missing or stale.

The port is also fixed: 8000. There is no port argument — different ports
across runs caused the same kind of confusion (two browser tabs pointing
at different builds, screenshots taken against the wrong instance). If
8000 is already in use, this script exits with a clear message instead of
silently picking another port.

Usage:
    tools/serve-static.py
"""
import errno
import os
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST_DIR = os.path.join(REPO_ROOT, "dist")
PORT = 8000


class NoCacheHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()


def main():
    if len(sys.argv) > 1:
        sys.exit(
            "error: serve-static.py takes no arguments. "
            f"It always serves {DIST_DIR} on port {PORT}."
        )
    if not os.path.isdir(DIST_DIR):
        sys.exit(f"error: {DIST_DIR} does not exist — run tools/build-static.sh first")
    # Bind directory via partial instead of os.chdir() — chdir'ing into a
    # path that gets deleted (e.g. by build-static.sh's rm -rf dist) leaves
    # the process in a "(deleted)" cwd, and every subsequent request fails
    # with FileNotFoundError on os.getcwd(). Passing directory= sidesteps
    # the problem entirely.
    handler = partial(NoCacheHandler, directory=DIST_DIR)
    addr = ("", PORT)
    try:
        server = ThreadingHTTPServer(addr, handler)
    except OSError as e:
        if e.errno == errno.EADDRINUSE:
            sys.exit(
                f"error: port {PORT} is already in use.\n"
                f"  Another wfweb static server (or other process) is bound to it.\n"
                f"  Find it:    lsof -i :{PORT}    or    fuser {PORT}/tcp\n"
                f"  Stop it, then re-run this script. The port is intentionally fixed."
            )
        raise
    print(f"wfweb static server (no-cache) → http://localhost:{PORT}")
    print(f"  serving: {DIST_DIR}")
    print("  Ctrl-C to stop")
    server.serve_forever()


if __name__ == "__main__":
    main()
