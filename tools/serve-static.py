#!/usr/bin/env python3
"""
Static file server for the wfweb dist that sends Cache-Control: no-store
on every response. python3 -m http.server (the default) caches aggressively
on the browser side, which makes iterative testing painful — every JS edit
needs a hard refresh, and a forgotten one looks like "the change didn't
work" when the dist on disk is fine.

Usage:
    tools/serve-static.py [port]                 # default port 8000
    tools/serve-static.py 8765 dist              # custom port + dir
"""
import os
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


class NoCacheHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    directory = sys.argv[2] if len(sys.argv) > 2 else os.getcwd()
    directory = os.path.abspath(directory)
    # Bind directory via partial instead of os.chdir() — chdir'ing into a
    # path that gets deleted (e.g. by build-static.sh's rm -rf dist) leaves
    # the process in a "(deleted)" cwd, and every subsequent request fails
    # with FileNotFoundError on os.getcwd(). Passing directory= sidesteps
    # the problem entirely.
    handler = partial(NoCacheHandler, directory=directory)
    addr = ("", port)
    print(f"wfweb static server (no-cache) → http://localhost:{port}")
    print(f"  serving: {directory}")
    print("  Ctrl-C to stop")
    ThreadingHTTPServer(addr, handler).serve_forever()


if __name__ == "__main__":
    main()
