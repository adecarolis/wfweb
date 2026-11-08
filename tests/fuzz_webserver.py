#!/usr/bin/env python3
"""Fuzz driver for wfweb's hand-written HTTP parser and its WebSocket handlers.

wfweb's HTTP side is its own parser on QTcpSocket / QSslSocket
(src/webserver.cpp: onHttpReadyRead and what it calls); the WebSocket side
is onWsTextMessage -> handleCommand and onWsBinaryMessage.  Shared Station
mode (--shared) puts both on the internet with a listen-only page open to
anonymous visitors, so everything a client can send before it is logged in
must be survivable.  This driver throws generated input at a running server
and watches the process: alive, and still answering GET /api/v1/radio/info
after every batch.

Two kinds of cases, both seeded and reproducible:
  * targeted: one case for every length / offset computation in the parser
    (Content-Length arithmetic, the 16 MB cap, the 4 KB upgrade peek, header
    lookups at the first / last line, path traversal into the resource tree,
    the sub-path arithmetic of the REST routes, odd-length audio frames, ...);
  * random: mutations of valid requests / commands, random bytes, wrong JSON
    types for every field of every WebSocket command.

Usage (stdlib + the websockets package the tests already use):
    tests/fuzz_webserver.py --port 8080 [--shared] [--seed N]
                            [--iterations N | --duration SECS] [--side http|ws|all]
                            [--pid PID] [--report FILE] [--cookie SESSION]

Exit status 0 when the server survived, 1 on a finding (details in --report
and on stderr: the batch that killed or hung the server, every input in it
as repr / base64).  tests/test_fuzz_webserver.py launches its own servers
(plain and --shared) with ASan / UBSan logs when the binary carries them
and runs a short pass; run this by hand with --duration 900 for a long one.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import random
import socket
import ssl
import struct
import sys
import time
import urllib.request

HEALTH_TIMEOUT = 10.0

# --- transport -------------------------------------------------------------

_TLS = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
_TLS.check_hostname = False
_TLS.verify_mode = ssl.CERT_NONE


def tls_connect(port, timeout=5.0):
    raw = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    try:
        return _TLS.wrap_socket(raw)
    except BaseException:
        raw.close()
        raise


def plain_connect(port, timeout=5.0):
    return socket.create_connection(("127.0.0.1", port), timeout=timeout)


def read_all(sock, timeout=3.0, limit=1 << 20):
    """Whatever the server answers until it closes or goes quiet."""
    sock.settimeout(timeout)
    buf = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and len(buf) < limit:
        try:
            chunk = sock.recv(65536)
        except ssl.SSLWantReadError:
            continue
        except OSError:
            break
        if not chunk:
            break
        buf += chunk
    return buf


def send_raw(port, data, *, tls, read=True, pieces=None, gap=0.0, timeout=3.0):
    """One connection, data (optionally in pieces with a pause), the answer."""
    try:
        s = tls_connect(port, timeout) if tls else plain_connect(port, timeout)
    except OSError as e:
        return b"<connect failed: %r>" % (e,)
    try:
        try:
            if pieces:
                for p in pieces:
                    s.sendall(p)
                    if gap:
                        time.sleep(gap)
            else:
                s.sendall(data)
        except OSError:
            pass    # the server may have cut us mid-send; that is its right
        if read:
            return read_all(s, timeout)
        return b""
    finally:
        try:
            s.close()
        except OSError:
            pass


def health(port, timeout=HEALTH_TIMEOUT):
    """True when GET /api/v1/radio/info on the plain REST port and on the
    HTTPS port both answer 200 within timeout."""
    deadline = time.monotonic() + timeout
    ok_plain = ok_tls = False
    while time.monotonic() < deadline and not (ok_plain and ok_tls):
        if not ok_plain:
            try:
                r = urllib.request.urlopen(f"http://127.0.0.1:{port + 1}/api/v1/radio/info", timeout=3)
                ok_plain = r.status == 200
            except Exception:
                pass
        if not ok_tls:
            try:
                r = urllib.request.urlopen(f"https://127.0.0.1:{port}/api/v1/radio/info", timeout=3, context=_TLS)
                ok_tls = r.status == 200
            except Exception:
                pass
        if not (ok_plain and ok_tls):
            time.sleep(0.2)
    return ok_plain and ok_tls


def pid_alive(pid):
    if not pid:
        return True
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    # A zombie (exited, not yet reaped by its parent) still answers kill(0).
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("State:"):
                    return "Z" not in line.split()[1]
    except OSError:
        pass
    return True


# --- HTTP cases -----------------------------------------------------------

def req(method="GET", path="/", headers=None, body=b"", version="HTTP/1.1", host=True, cl=True):
    """A request as bytes.  headers: list of (name, value) or raw lines."""
    lines = [f"{method} {path} {version}".encode() if version else f"{method} {path}".encode()]
    if host:
        lines.append(b"Host: 127.0.0.1")
    for h in headers or []:
        lines.append(h if isinstance(h, bytes) else f"{h[0]}: {h[1]}".encode())
    if body and cl:
        lines.append(b"Content-Length: %d" % len(body))
    return b"\r\n".join(lines) + b"\r\n\r\n" + body


MB = 1024 * 1024
HTTP_MAX = 16 * MB

STATIC_PATHS = ["/", "/index.html", "/theme.css", "/favicon.svg", "/models/deepcw.json",
                "/ort/ort.min.js", "/packet.js", "/wasm/js8.wasm"]
API_PATHS = ["/api/v1/radio/info", "/api/v1/radio/status", "/api/v1/radio/frequency",
             "/api/v1/radio/mode", "/api/v1/radio/vfo", "/api/v1/radio/ptt", "/api/v1/radio/meters",
             "/api/v1/radio/gains", "/api/v1/radio/rx", "/api/v1/radio/tx", "/api/v1/radio/cw",
             "/api/v1/radio/memories", "/api/v1/radio/memories/1/recall", "/api/v1/radio",
             "/api/v1/station", "/api/v1/cw/macros", "/api/v1/logbook", "/api/v1/logbook/adif",
             "/api/v1/logbook/worked", "/api/v1/logbook/export", "/api/v1/logbook/exported",
             "/api/v1/logbook/x", "/api/v1/sstv/images", "/api/v1/sstv/images/x",
             "/api/v1/auth/me", "/api/v1/auth/login", "/api/v1/auth/logout", "/api/v1/auth/setup",
             "/api/v1/auth/tokens", "/api/v1/auth", "/api/v1/shared/users", "/api/v1/shared/users/x",
             "/api/v1/shared/policy", "/api/v1/shared/connections", "/api/v1/shared/connections/x",
             "/api/v1/shared/reservations", "/api/v1/shared/reservations/x", "/api/v1/shared/seat",
             "/api/v1/", "/api/v1/nothing", "/api/v2/radio/info"]
METHODS = ["GET", "POST", "PUT", "DELETE", "OPTIONS", "HEAD", "PATCH", "TRACE", "CONNECT", "get", "G\x00T", ""]

JSON_ODD = [
    b"", b"{", b"}", b"[]", b"[1,2,3]", b"null", b"true", b"1e999", b"-1e999", b"\"str\"",
    b"{\"a\":}", b"{\"a\":1,}", b"{\"a\":NaN}", b"{\"a\":Infinity}", b"{\"a\":-}", b"\xff\xfe{}",
    b"{\"\\ud800\":1}", b"{\"a\":\"\\ud800\"}", b"{\"a\":\"\\u0000\"}", b"{\"a\":1}\x00",
    b"[" * 2000 + b"]" * 2000, b"{\"a\":" * 1500 + b"1" + b"}" * 1500,
    b"{\"callsign\":12345,\"grid\":[1,2],\"macros\":\"x\",\"limit\":-1}",
    b"{\"callsign\":\"" + b"A" * 100000 + b"\"}",
    b"{\"a\":1" + b",\"a\":2" * 10000 + b"}",
    b"{\"username\":{\"$ne\":1},\"password\":[]}",
    b"{\"username\":\"\",\"password\":\"\"}",
    b"{\"username\":\"" + b"u" * 10000 + b"\",\"password\":\"" + b"p" * 10000 + b"\"}",
    b"{\"code\":\"ABCD-EFGH\",\"username\":\"x\",\"password\":\"y\",\"callsign\":null}",
    b"{\"value\":1e30}", b"{\"value\":-1}", b"{\"value\":\"7074000\"}", b"{\"hz\":1e30}",
    b"{\"hz\":-7074000}", b"{\"hz\":18446744073709551616}", b"{\"mode\":\"\"}", b"{\"mode\":12}",
    b"{\"filter\":99}", b"{\"vfo\":\"Q\"}", b"{\"ptt\":\"yes\"}", b"{\"group\":-5}",
    b"{\"macros\":{\"1\":null,\"x\":[1],\"0\":\"\",\"11\":\"a\",\"-1\":\"b\"}}",
    b"{\"anonymous\":\"root\",\"maxClients\":-1,\"txTimeoutSecs\":1e30,\"seat\":\"x\"}",
    b"{\"role\":\"god\",\"username\":\"../../etc\",\"password\":\"x\"}",
    b"{\"start\":\"2026-13-45T99:99:99Z\",\"repeat\":\"never\",\"minutes\":-1}",
]

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def targeted_http_cases(rng, shared, quick=False):
    """(label, bytes, options) for every computation seen in the parser.
    quick: the odd-JSON x route matrix (the bulk of the list) is sampled,
    eight bodies per route instead of all of them."""
    cases = []
    add = lambda label, data, **kw: cases.append((label, data, kw))

    # Request line
    add("empty line", b"\r\n\r\n")
    add("just CRLFs", b"\r\n" * 50)
    add("one token", b"GET\r\n\r\n")
    add("one token + spaces", b"GET      \r\n\r\n")
    add("no version", req(version=""))
    add("many spaces", b"GET " + b" " * 1000 + b"/ HTTP/1.1\r\n\r\n")
    add("lf only", b"GET / HTTP/1.1\nHost: x\n\n")
    add("cr only", b"GET / HTTP/1.1\rHost: x\r\r")
    add("mixed eol then crlfcrlf", b"GET / HTTP/1.1\nHost: x\r\n\r\n")
    add("binary method", b"\x00\x01\x02 / HTTP/1.1\r\n\r\n")
    add("nul in path", b"GET /ind\x00ex.html HTTP/1.1\r\n\r\n")
    add("utf8 path", "GET /ünïcødé/../index.html HTTP/1.1\r\n\r\n".encode())
    add("invalid utf8 path", b"GET /\xff\xfe\xc0\xaf HTTP/1.1\r\n\r\n")
    add("absolute-form", req(path="http://127.0.0.1/index.html"))
    add("asterisk", req(method="OPTIONS", path="*"))
    add("long path 64k", req(path="/" + "a" * 65536))
    add("long path 8M", req(path="/" + "a" * (8 * MB)))
    add("long query", req(path="/api/v1/logbook?" + "&".join(f"k{i}=v{i}" for i in range(20000))))
    add("query odd", req(path="/api/v1/logbook?limit=-1&before=|&call=%00%ff%"))
    add("query only ?", req(path="?"))
    add("query ??", req(path="/api/v1/logbook??limit=1"))
    add("query percent broken", req(path="/api/v1/logbook?before=%zz%2&limit=%41"))
    add("logbook before bar only", req(path="/api/v1/logbook?before=|"))
    add("logbook before many bars", req(path="/api/v1/logbook?before=|||||"))
    add("logbook before huge", req(path="/api/v1/logbook?before=" + "x" * 100000 + "|" + "y" * 100000))
    add("logbook limit huge", req(path="/api/v1/logbook?limit=99999999999999999999"))
    add("logbook id empty", req(method="DELETE", path="/api/v1/logbook/"))
    add("logbook id odd", req(method="PUT", path="/api/v1/logbook/../../x", body=b"{}"))

    # Static path handling
    for p in ["/..", "/../", "/../../", "/../qt-project.org/qmime/freedesktop.org.xml",
              "/../qt-project.org/qmime/packages/freedesktop.org.xml", "/./index.html", "//index.html",
              "/web/../index.html", "/%2e%2e/%2e%2e/etc/passwd", "/..%2f..%2f", "/index.html/..",
              "/index.html/../theme.css", "/models/../index.html", "/ort/../../qt-project.org/",
              "/\\..\\..\\index.html", "/index.html%00.png", "/index.html?x=/../", "/.", "/./", "/...",
              "/....//index.html", "/%", "/%2", "/%G0", "/" + "../" * 500 + "index.html",
              "/" + "/" * 1000, "/index.html" + "/" * 1000, "/\x7f", "/ index.html"]:
        add(f"static {p[:40]!r}", req(path=p))
    add("If-None-Match last line", b"GET /models/deepcw.json HTTP/1.1\r\nHost: x\r\nIf-None-Match: \"x\"\r\n\r\n")
    add("If-None-Match empty", b"GET /models/deepcw.json HTTP/1.1\r\nIf-None-Match:\r\n\r\n")
    add("If-None-Match first", b"If-None-Match: *\r\nGET /models/deepcw.json HTTP/1.1\r\n\r\n")
    add("If-None-Match huge", req(path="/models/deepcw.json", headers=[("If-None-Match", "x" * 100000)]))
    add("If-None-Match star", req(path="/ort/ort.min.js", headers=[("If-None-Match", "*")]))

    # Header block
    add("header no colon", b"GET / HTTP/1.1\r\nHost\r\n\r\n")
    add("header colon only", b"GET / HTTP/1.1\r\n:\r\n\r\n")
    add("header empty name", b"GET / HTTP/1.1\r\n: value\r\n\r\n")
    add("header 10k lines", req(headers=[(f"X-{i}", "v") for i in range(10000)]))
    add("header 1 MB value", req(headers=[("X-Big", "v" * MB)]))
    add("header continuation", b"GET / HTTP/1.1\r\nHost: x\r\n  continued\r\n\r\n")
    add("header nul", b"GET / HTTP/1.1\r\nHost: x\x00y\r\n\r\n")
    add("header high bytes", b"GET / HTTP/1.1\r\nHost: \xff\xfe\r\nUser-Agent: \xc0\xaf\r\n\r\n")
    add("host only line", b"Host: x\r\n\r\n")
    add("content-length in request line", b"GET /content-length:5 HTTP/1.1\r\n\r\n")
    add("content-length in other header", b"GET /api/v1/station HTTP/1.1\r\nX-A: content-length: 5\r\n\r\nabcde")
    add("double content-length", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 20\r\n\r\n{}")
    add("double content-length 2", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 20\r\nContent-Length: 2\r\n\r\n{}")
    add("content-length last no crlf", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}")
    add("content-length first line", b"Content-Length: 2\r\nPOST /api/v1/station HTTP/1.1\r\n\r\n{}")
    add("content-length empty", b"POST /api/v1/station HTTP/1.1\r\nContent-Length:\r\n\r\n{}")
    add("content-length spaces", b"POST /api/v1/station HTTP/1.1\r\nContent-Length:     2    \r\n\r\n{}")
    add("content-length negative", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: -1\r\n\r\n{}")
    add("content-length -2^31", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: -2147483648\r\n\r\n{}")
    add("content-length 2^31-1", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 2147483647\r\n\r\n{}")
    add("content-length 2^31", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 2147483648\r\n\r\n{}")
    add("content-length 2^32", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 4294967296\r\n\r\n{}")
    add("content-length 2^63", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 9223372036854775808\r\n\r\n{}")
    add("content-length 40 digits", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: " + b"9" * 40 + b"\r\n\r\n{}")
    add("content-length plus", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: +2\r\n\r\n{}")
    add("content-length hex", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 0x10\r\n\r\n{}")
    add("content-length float", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 2.5\r\n\r\n{}")
    add("content-length text", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: two\r\n\r\n{}")
    add("content-length 16M exactly", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: %d\r\n\r\n" % HTTP_MAX)
    add("content-length 16M+1", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: %d\r\n\r\n" % (HTTP_MAX + 1))
    add("body longer than cl", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}" + b"GARBAGE" * 100)
    add("body shorter than cl (wait)", b"POST /api/v1/station HTTP/1.1\r\nContent-Length: 100\r\n\r\n{}", timeout=1.0)
    add("body 15 MB", req(method="POST", path="/api/v1/station", body=b"{" + b" " * (15 * MB) + b"}"), timeout=20.0)
    add("body 16 MB - headers", req(method="POST", path="/api/v1/station", body=b"x" * (HTTP_MAX - 60)), timeout=20.0)
    add("body 16 MB + 1 no cl", b"POST /api/v1/station HTTP/1.1\r\n\r\n" + b"x" * (HTTP_MAX + 1), timeout=20.0)
    add("headers 16 MB + 1 no end", b"GET / HTTP/1.1\r\nX: " + b"x" * (HTTP_MAX + 1), timeout=20.0)
    add("chunked", b"POST /api/v1/station HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\n{\"a\":\r\n0\r\n\r\n")
    add("chunked + cl", b"POST /api/v1/station HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\n{}")
    add("expect 100", req(method="POST", path="/api/v1/station", headers=[("Expect", "100-continue")], body=b"{}"))
    add("pipelined", req(path="/api/v1/radio/info") + req(path="/api/v1/radio/status") + req(path="/"))
    add("pipelined static", req(path="/") * 20)
    add("post to static", req(method="POST", path="/index.html", body=b"x"))
    add("head", req(method="HEAD", path="/"))
    add("options static", req(method="OPTIONS", path="/"))

    # Split delivery: the parser's Content-Length wait and header wait
    r = req(method="POST", path="/api/v1/station", body=b'{"callsign":"K1FM"}')
    for cut in (1, 4, 5, 20, len(r) - 25, len(r) - 20, len(r) - 19, len(r) - 1):
        cut = max(1, min(len(r) - 1, cut))
        add(f"split at {cut}", r, pieces=[r[:cut], r[cut:]], gap=0.2)
    add("byte by byte", r[:60], pieces=[bytes([b]) for b in r[:60]] + [r[60:]], gap=0.01)

    # Cookie / Authorization / X-Forwarded-For (requestContext, access.cpp)
    for cookie in ["", ";", ";;;", "=", "wfweb_session", "wfweb_session=", "wfweb_session==",
                   "wfweb_session=;", "a=b; wfweb_session=" + "f" * 10000, "wfweb_session=\x00\xff",
                   "wfweb_session=x; wfweb_session=y", ";wfweb_session=x", "wfweb_sessionx=1",
                   "xwfweb_session=1", "=" * 1000, "; " * 5000]:
        add(f"cookie {cookie[:30]!r}", req(path="/api/v1/auth/me", headers=[("Cookie", cookie)]))
    for auth in ["", "Bearer", "Bearer ", "Bearer  ", "bearer x", "BEARER\tx", "Basic eDp5", "Bearer " + "x" * 100000,
                 "Bearer \x00", "Bearerx", "Bearer x y z", " Bearer x "]:
        add(f"auth {auth[:30]!r}", req(path="/api/v1/radio/info", headers=[("Authorization", auth)]))
    for xff in ["", ",", ",,,,", "1.2.3.4", "[::1]:443", "[::1", "::1]", "1.2.3.4:443:1", "[", "]",
                "999.999.999.999", "::ffff:1.2.3.4", "a" * 100000, "1.2.3.4," * 10000, "[" * 1000]:
        add(f"xff {xff[:30]!r}", req(path="/api/v1/auth/me", headers=[("X-Forwarded-For", xff), ("X-Forwarded-Proto", "https")]))

    # WebSocket upgrade peek (4 KB, Origin / Host check)
    def upgrade(extra, pad=0, origin=None, host="127.0.0.1"):
        lines = [b"GET / HTTP/1.1"]
        if host is not None:
            lines.append(b"Host: " + host.encode())
        lines += [b"Upgrade: websocket", b"Connection: Upgrade",
                  b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", b"Sec-WebSocket-Version: 13"]
        if origin is not None:
            lines.append(b"Origin: " + origin.encode())
        if pad:
            lines.append(b"X-Pad: " + b"p" * pad)
        lines += extra
        return b"\r\n".join(lines) + b"\r\n\r\n"
    add("upgrade plain", upgrade([]))
    add("upgrade lowercase header", b"GET / HTTP/1.1\r\nupgrade: websocket\r\nconnection: upgrade\r\n\r\n")
    add("upgrade WebSocket case", upgrade([]).replace(b"websocket", b"WebSocket"))
    add("upgrade no key", b"GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n")
    add("upgrade bad version", upgrade([]).replace(b"13", b"99"))
    add("upgrade key garbage", upgrade([]).replace(b"dGhlIHNhbXBsZSBub25jZQ==", b"\xff\xfe\x00"))
    add("upgrade key 100k", upgrade([]).replace(b"dGhlIHNhbXBsZSBub25jZQ==", b"A" * 100000))
    add("upgrade POST", upgrade([]).replace(b"GET", b"POST"))
    add("upgrade in body", req(method="POST", path="/api/v1/station", body=b"Upgrade: websocket"))
    add("upgrade in path", req(path="/Upgrade:%20websocket"))
    add("upgrade in query raw", b"GET /?Upgrade: websocket HTTP/1.1\r\nHost: x\r\n\r\n")
    add("upgrade no host", upgrade([], host=None))
    add("upgrade empty host", upgrade([], host=""))
    for o in ["", "null", "http://", "://", "a://b://c", "[::1", "https://[::1]:8080", "https://127.0.0.1:8080",
              "HTTPS://127.0.0.1", "http://127.0.0.1@evil", "http://evil#127.0.0.1", "http://127.0.0.1.evil",
              "https://127.0.0.1\x00evil", "http://" + "a" * 10000, "\xff", "javascript:", "file://"]:
        add(f"upgrade origin {o[:30]!r}", upgrade([], origin=o))
    for h in ["", ":", ":8080", "127.0.0.1:", "127.0.0.1:x", "[", "[::1]:8080", "a" * 10000, "127.0.0.1:8080:9090"]:
        add(f"upgrade host {h[:30]!r}", upgrade([], origin="https://127.0.0.1", host=h))
    # The peek is 4096 bytes: headers that end just inside, at, past it; the
    # Upgrade line itself across the boundary; no end at all.
    for pad in (4000, 4040, 4050, 4060, 4070, 4080, 4090, 4096, 4100, 4200, 8192, 70000):
        add(f"upgrade padded {pad}", upgrade([], pad=pad))
    big = b"GET / HTTP/1.1\r\nHost: x\r\nX-Pad: " + b"p" * 4060 + b"\r\nUpgrade: websocket\r\n\r\n"
    add("upgrade past peek", big)
    for cut in (4080, 4085, 4090, 4094):
        add(f"upgrade split at {cut}", big, pieces=[big[:cut], big[cut:]], gap=0.3)
    add("upgrade headers never end", b"GET / HTTP/1.1\r\nUpgrade: websocket\r\nX: " + b"y" * 5000, timeout=1.0)
    add("upgrade headers never end short", b"GET / HTTP/1.1\r\nUpgrade: websocket\r\n", timeout=1.0)
    add("upgrade then cl body", upgrade([b"Content-Length: 5"]) + b"hello")
    add("upgrade + 16M", upgrade([b"Content-Length: %d" % (HTTP_MAX + 1)]))

    # REST routes: sub-path arithmetic, bodies
    for p in ["/api/v1/radio/memories//recall", "/api/v1/radio/memories/recall", "/api/v1/radio/memories/-1/recall",
              "/api/v1/radio/memories/99999999999/recall", "/api/v1/radio/memories/1/2/recall",
              "/api/v1/radio/memories/x/recall", "/api/v1/radio/memories/1/recall/", "/api/v1/radio/memories/1recall",
              "/api/v1/radio/memories/" + "1" * 10000 + "/recall"]:
        add(f"memories {p[:40]}", req(method="POST", path=p, body=b'{"group":-1}'))
    for p in ["/api/v1/auth", "/api/v1/auth/", "/api/v1/auth/x", "/api/v1/authx", "/api/v1/auth/me/", "/api/v1/auth/../radio/info",
              "/api/v1/auth/tokens/", "/api/v1/auth/tokens/x", "/api/v1/auth/tokens/" + "x" * 10000,
              "/api/v1/shared/users/", "/api/v1/shared/usersx", "/api/v1/shared/users/../policy",
              "/api/v1/shared/connections/", "/api/v1/shared/connectionsx", "/api/v1/shared/reservations/",
              "/api/v1/shared/reservationsx", "/api/v1/shared/seat/", "/api/v1/shared/", "/api/v1/shared"]:
        for m in ("GET", "POST", "PUT", "DELETE"):
            add(f"{m} {p[:40]}", req(method=m, path=p, body=rng.choice(JSON_ODD)))
    for p in API_PATHS:
        for body in (rng.sample(JSON_ODD, 8) if quick else JSON_ODD):
            add(f"json {p} {body[:20]!r}", req(method=rng.choice(["POST", "PUT"]), path=p, body=body))
    # SSTV gallery
    for body in [b"", b"\x89PNG", PNG_MAGIC, PNG_MAGIC + b"\x00" * 100, b"x" * 4 * MB, PNG_MAGIC + b"x" * (4 * MB),
                 PNG_MAGIC + b"x" * (4 * MB - 7)]:
        add(f"sstv post {len(body)}", req(method="POST",
            path="/api/v1/sstv/images?ts=" + rng.choice(["x", "2026-13-45T25:61:61.999Z", "", "9999999999-01-01T00:00:00Z"])
                 + "&kind=../x&name=" + "n" * 1000 + "&mode=" + "%00" * 50 + "&freq=-1e99&w=-1&h=99999999999999",
            body=body), timeout=20.0)
    for i in ["..", "../..", "x/../y", "%2e%2e", "a" * 65, "é", "日本", "x.png.png", ".png", "-", "a-b", "x%00.png",
              "20260101T000000Z-0000", "20260101T000000Z-0000.png", "x/", "/", "x?y=1"]:
        for m in ("GET", "DELETE", "PUT"):
            add(f"sstv {m} {i[:20]!r}", req(method=m, path="/api/v1/sstv/images/" + i))

    # Plain-REST-port signpost page and the TLS layer itself
    add("random bytes 1k", bytes(rng.getrandbits(8) for _ in range(1024)))
    add("random bytes 64k", bytes(rng.getrandbits(8) for _ in range(65536)))
    add("tls-looking bytes", b"\x16\x03\x01\x00\xff" + bytes(rng.getrandbits(8) for _ in range(300)))
    add("http/0.9", b"GET /\r\n")
    add("http/2 preface", b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n")
    add("only body", b"{}")
    return cases


def mutate(rng, data):
    """A few random edits to a valid request / command."""
    b = bytearray(data)
    for _ in range(rng.randint(1, 8)):
        op = rng.random()
        if not b:
            b = bytearray(b"GET / HTTP/1.1\r\n\r\n")
        i = rng.randrange(len(b))
        if op < 0.3:
            b[i] = rng.getrandbits(8)
        elif op < 0.5:
            del b[i:i + rng.randint(1, 16)]
        elif op < 0.7:
            b[i:i] = bytes(rng.getrandbits(8) for _ in range(rng.randint(1, 64)))
        elif op < 0.85:
            j = rng.randrange(len(b))
            b[i:i] = b[j:j + rng.randint(1, 200)]
        else:
            token = rng.choice([b"\r\n", b"\r\n\r\n", b"Content-Length: ", b"Upgrade: websocket", b"..",
                                b"%", b"\x00", b":", b" ", b"Cookie: wfweb_session=", b"Authorization: Bearer "])
            b[i:i] = token
    return bytes(b)


def random_http_case(rng):
    """A mutated valid request, or a request built from random parts."""
    if rng.random() < 0.5:
        base = req(method=rng.choice(["GET", "POST", "PUT", "DELETE"]),
                   path=rng.choice(STATIC_PATHS + API_PATHS),
                   headers=[("Cookie", "wfweb_session=abc"), ("Authorization", "Bearer abc"),
                            ("X-Forwarded-For", "1.2.3.4"), ("Origin", "https://127.0.0.1")],
                   body=rng.choice(JSON_ODD + [b'{"callsign":"K1FM","grid":"FN42"}']))
        return "mutated", mutate(rng, base)
    method = rng.choice(METHODS)
    path = rng.choice(STATIC_PATHS + API_PATHS)
    if rng.random() < 0.3:
        path += "?" + "".join(rng.choice("abc=&%?/.\x00") for _ in range(rng.randint(0, 50)))
    if rng.random() < 0.2:
        path = path + "".join(rng.choice("/.%\\") for _ in range(rng.randint(1, 20)))
    headers = []
    for _ in range(rng.randint(0, 12)):
        name = rng.choice(["Host", "Content-Length", "Cookie", "Authorization", "Upgrade", "Connection", "Origin",
                           "X-Forwarded-For", "X-Forwarded-Proto", "If-None-Match", "Transfer-Encoding", "User-Agent",
                           "".join(rng.choice("abcXYZ-_") for _ in range(rng.randint(0, 30)))])
        value = rng.choice(["", " ", "0", "-1", "99999999999", "websocket", "x" * rng.randint(0, 5000),
                            "wfweb_session=" + "x" * rng.randint(0, 100), "Bearer " + "x" * rng.randint(0, 100),
                            "127.0.0.1", "1.2.3.4, 5.6.7.8", "https", "\xff\x00", "chunked"])
        headers.append((name, value))
    body = rng.choice(JSON_ODD + [b""])
    cl = rng.random() < 0.8
    return "random", req(method, path, headers, body, host=rng.random() < 0.9, cl=cl)


# --- WebSocket cases -------------------------------------------------------

WS_COMMANDS = [
    "aprsBeaconConfig", "aprsClearStations", "aprsRequestSnapshot", "aprsTxBeacon", "clearMemory", "deleteQso",
    "disconnectLan", "enableAudio", "enableMic", "equalizeVFO", "getMemories", "getStatus", "mergeLogbook",
    "packetEnable", "packetSetMode", "packetTx", "qsoLogged", "recallMemory", "reconnectLan", "renameMemory",
    "reportDigiSpot", "seatAccept", "seatCancel", "seatClear", "seatHandover", "seatPrivate", "seatRelease",
    "seatTake", "selectVFO", "sendCW", "setAfGain", "setAGC", "setAntenna", "setAttenuator", "setAutoNotch",
    "setBand", "setBreakIn", "setCompLevel", "setCompressor", "setCWMacros", "setCWSpeed", "setDigiActive",
    "setDtcsCode", "setDuplex", "setDuplexOffset", "setFilter", "setFilterShape", "setFilterWidth", "setFreeDV",
    "setFreeDVMonitor", "setFreeDVTxGain", "setFrequency", "setMicGain", "setMode", "setMonitor", "setMonitorGain",
    "setNoiseBlanker", "setNoiseReduction", "setPBTInner", "setPBTOuter", "setPower", "setPreamp", "setPTT",
    "setReporter", "setRfGain", "setRfPower", "setScopeEdges", "setScopeMode", "setScopeRef", "setScopeSpeed",
    "setSpan", "setSplit", "setSquelch", "setStationCallsign", "setToneFreq", "setToneMode", "setTsqlFreq",
    "setTuner", "setTxMeter", "stopCW", "swapVFO", "termClose", "termConnect", "termDisconnect", "termFileAbort",
    "termFileSend", "termHistory", "termList", "termRegister", "termSend", "termUnregister", "termXferAccept",
    "termXferReject", "updateQso", "writeMemory",
]
# Commands that act on the radio or the station and are left out of the
# plain-server WS run (they would transmit on a real rig; the bench is a
# mock, but the driver is also pointed at hand-run servers).
WS_SKIP_PLAIN = {"setPTT", "sendCW", "aprsTxBeacon", "packetTx", "disconnectLan", "reconnectLan", "termFileSend",
                 "setFreeDV", "setReporter", "setStationCallsign", "enableMic", "termConnect"}
WS_FIELDS = ["value", "cmd", "channel", "group", "name", "text", "wpm", "sid", "data", "dataB64", "src", "dst",
             "info", "path", "macros", "active", "mode", "callsign", "grid", "snr", "freq", "antenna", "rx",
             "lower", "upper", "bsr", "reg", "save", "start", "end", "id", "qso", "entries", "polarity",
             "filter", "enabled", "digital", "baud", "force", "accept", "to", "limit", "seconds", "private",
             "frequency", "memory", "vfo", "width", "shape", "offset", "tone", "code", "level", "gain"]
WS_VALUES = [None, True, False, 0, 1, -1, 2 ** 31 - 1, 2 ** 31, -2 ** 31 - 1, 2 ** 53, 2 ** 63, 2 ** 64, -2 ** 63,
             1e308, -1e308, 1e-308, 0.5, -0.5, 7074000, 7074000.5, 1e30, "", "x", "7074000", "-1", "1e999",
             "USB", "CW", "\x00", "\udcff", "日本", "A" * 10000, [], [1, 2, 3], [None] * 1000, {}, {"a": 1},
             {"value": {"value": {"value": 1}}}, [[[[[[[[[[]]]]]]]]]], "../../etc/passwd", "WIDE1-1", "K1FM",
             "%s%s%n", "<script>", "\\", "\"", "{}", "[]", "null", "true", 99, 100, 255, 256, 999, 65535, 65536]
WS_RAW_TEXT = [
    "", " ", "{", "}", "[]", "[1]", "null", "1", "\"x\"", "{\"cmd\":}", "{\"cmd\":\"getStatus\",}",
    "{\"cmd\":\"getStatus\"}\x00", "{\"cmd\":1}", "{\"cmd\":null}", "{\"cmd\":[]}", "{\"cmd\":{}}", "{\"cmd\":true}",
    "{\"cmd\":\"\"}", "{\"cmd\":\"" + "x" * 100000 + "\"}", "{\"cmd\":\"nosuchcommand\"}", "{\"cmd\":\"setfrequency\"}",
    "{\"cmd\":\"setFrequency\",\"value\":1e999}", "{\"cmd\":\"setFrequency\",\"value\":-1e999}",
    "{\"cmd\":\"setFrequency\",\"value\":NaN}", "{\"cmd\":\"setFrequency\",\"value\":1e30}",
    "{\"cmd\":\"setFrequency\",\"value\":18446744073709551616}", "{\"cmd\":\"setFrequency\",\"value\":-5}",
    "{\"cmd\":\"setScopeEdges\",\"lower\":-1,\"upper\":1e30}", "{\"cmd\":\"setScopeEdges\",\"lower\":1e999,\"upper\":-1e999}",
    "{\"cmd\":\"setScopeEdges\",\"lower\":\"x\",\"upper\":null}", "{\"cmd\":\"setDuplexOffset\",\"value\":-1e308}",
    "{\"cmd\":\"setDuplexOffset\",\"value\":1e999}", "{\"cmd\":\"setSpan\",\"value\":-1}", "{\"cmd\":\"setSpan\",\"value\":2147483647}",
    "{\"cmd\":\"setFilter\",\"value\":-1}", "{\"cmd\":\"setFilter\",\"value\":2147483647}",
    "{\"cmd\":\"recallMemory\",\"channel\":-1,\"group\":-1}", "{\"cmd\":\"recallMemory\",\"channel\":2147483647,\"group\":2147483647}",
    "{\"cmd\":\"getMemories\",\"start\":-2147483648,\"end\":2147483647}", "{\"cmd\":\"getMemories\",\"start\":100,\"end\":-100}",
    "{\"cmd\":\"writeMemory\",\"channel\":0,\"name\":\"" + "n" * 1000 + "\"}",
    "{\"cmd\":\"writeMemory\",\"channel\":2147483647,\"name\":\"\\u0000\\uffff\"}",
    "{\"cmd\":\"renameMemory\",\"channel\":-2147483648,\"name\":\"x\"}", "{\"cmd\":\"clearMemory\",\"channel\":-1}",
    "{\"cmd\":\"setAntenna\",\"antenna\":-1}", "{\"cmd\":\"setAntenna\",\"antenna\":999999999}",
    "{\"cmd\":\"setBand\",\"bsr\":-1,\"reg\":-1,\"save\":{\"bsr\":-1,\"reg\":99}}", "{\"cmd\":\"setBand\",\"bsr\":1,\"reg\":1,\"save\":[]}",
    "{\"cmd\":\"setMode\",\"value\":\"\"}", "{\"cmd\":\"setMode\",\"value\":\"" + "M" * 10000 + "\",\"filter\":-1}",
    "{\"cmd\":\"setMode\",\"value\":12}", "{\"cmd\":\"setMode\"}", "{\"cmd\":\"setCWSpeed\",\"value\":-1}",
    "{\"cmd\":\"setCWMacros\",\"macros\":{\"0\":\"a\",\"11\":\"b\",\"-1\":\"c\",\"x\":null,\"1\":[1]}}",
    "{\"cmd\":\"setCWMacros\",\"macros\":[]}", "{\"cmd\":\"setCWMacros\",\"macros\":\"x\"}",
    "{\"cmd\":\"qsoLogged\",\"qso\":[]}", "{\"cmd\":\"qsoLogged\",\"qso\":{\"call\":\"\",\"freq\":-1}}",
    "{\"cmd\":\"qsoLogged\",\"qso\":{\"call\":\"" + "X" * 100000 + "\"}}", "{\"cmd\":\"updateQso\",\"id\":\"\",\"qso\":1}",
    "{\"cmd\":\"updateQso\",\"id\":\"../x\"}", "{\"cmd\":\"deleteQso\",\"id\":null}", "{\"cmd\":\"deleteQso\"}",
    "{\"cmd\":\"mergeLogbook\",\"entries\":\"x\"}", "{\"cmd\":\"mergeLogbook\",\"entries\":[1,\"x\",null,[],{}]}",
    "{\"cmd\":\"mergeLogbook\",\"entries\":[" + ",".join(["{}"] * 50000) + "]}",
    "{\"cmd\":\"termRegister\",\"sid\":\"\"}", "{\"cmd\":\"termSend\",\"sid\":\"x\",\"data\":\"" + "d" * 100000 + "\"}",
    "{\"cmd\":\"termSend\"}", "{\"cmd\":\"termHistory\",\"sid\":\"../x\"}", "{\"cmd\":\"termXferAccept\",\"sid\":\"x\"}",
    "{\"cmd\":\"termFileAbort\",\"sid\":null}", "{\"cmd\":\"termDisconnect\",\"sid\":1}", "{\"cmd\":\"termClose\",\"sid\":[]}",
    "{\"cmd\":\"termUnregister\",\"sid\":{}}", "{\"cmd\":\"termList\"}", "{\"cmd\":\"packetSetMode\",\"mode\":\"x\",\"baud\":-1}",
    "{\"cmd\":\"packetEnable\",\"enabled\":\"x\"}", "{\"cmd\":\"aprsBeaconConfig\",\"interval\":-1,\"path\":[],\"comment\":\"" + "c" * 10000 + "\"}",
    "{\"cmd\":\"aprsBeaconConfig\",\"lat\":1e30,\"lon\":-1e30,\"symbol\":\"\"}", "{\"cmd\":\"reportDigiSpot\",\"snr\":-999999,\"freq\":1e30}",
    "{\"cmd\":\"reportDigiSpot\",\"callsign\":\"" + "c" * 10000 + "\",\"mode\":[]}", "{\"cmd\":\"setDigiActive\",\"active\":1,\"mode\":null}",
    "{\"cmd\":\"setToneFreq\",\"value\":-1}", "{\"cmd\":\"setToneFreq\",\"value\":65536}", "{\"cmd\":\"setDtcsCode\",\"value\":-1,\"polarity\":99}",
    "{\"cmd\":\"setTxMeter\",\"value\":-1}", "{\"cmd\":\"setTxMeter\",\"value\":\"x\"}", "{\"cmd\":\"selectVFO\",\"value\":\"Q\"}",
    "{\"cmd\":\"setScopeMode\",\"value\":-1}", "{\"cmd\":\"setScopeRef\",\"value\":1e30}", "{\"cmd\":\"setScopeSpeed\",\"value\":-1}",
    "{\"cmd\":\"setFilterWidth\",\"value\":-1}", "{\"cmd\":\"setFilterShape\",\"value\":99}", "{\"cmd\":\"setPBTInner\",\"value\":1e30}",
    "{\"cmd\":\"setAfGain\",\"value\":-1}", "{\"cmd\":\"setAfGain\",\"value\":1e30}", "{\"cmd\":\"setRfPower\",\"value\":256}",
    "{\"cmd\":\"enableAudio\",\"value\":\"true\"}", "{\"cmd\":\"enableAudio\",\"value\":1}", "{\"cmd\":\"getStatus\",\"x\":" + "[" * 1000 + "]" * 1000 + "}",
    "{\"cmd\":\"getStatus\"," + ",".join(f"\"k{i}\":{i}" for i in range(20000)) + "}",
    "{\"cmd\":\"seatTake\",\"force\":\"yes\"}", "{\"cmd\":\"seatHandover\",\"accept\":1}", "{\"cmd\":\"seatHandover\"}",
    "{\"cmd\":\"seatPrivate\",\"value\":null}", "{\"cmd\":\"seatClear\"}", "{\"cmd\":\"seat\"}", "{\"cmd\":\"seatX\"}",
    "{\"cmd\":\"seatTake\",\"force\":true}", "{\"cmd\":\"seatRelease\"}", "{\"cmd\":\"seatCancel\"}", "{\"cmd\":\"seatAccept\"}",
    "{\"cmd\":\"\\ud800\"}", "{\"cmd\":\"getStatus\",\"x\":\"\\ud800\\udc00\"}", "{\"\\u0000cmd\":\"getStatus\"}",
    "{\"cmd\":\"getStatus\",\"cmd\":\"nosuch\"}", "{\"cmd\":\"nosuch\",\"cmd\":\"getStatus\"}",
    "\xef\xbb\xbf{\"cmd\":\"getStatus\"}", "{\"cmd\":\"getStatus\"} {\"cmd\":\"getStatus\"}", "{\"cmd\":\"getStatus\"}\n",
]


def random_ws_command(rng, shared):
    """A command with random fields of random types."""
    pool = WS_COMMANDS if shared else [c for c in WS_COMMANDS if c not in WS_SKIP_PLAIN]
    cmd = {"cmd": rng.choice(pool + ["nosuch", "", "GETSTATUS"])}
    for _ in range(rng.randint(0, 6)):
        cmd[rng.choice(WS_FIELDS)] = rng.choice(WS_VALUES)
    if rng.random() < 0.1:
        cmd["cmd"] = rng.choice(WS_VALUES)
    try:
        return json.dumps(cmd)
    except (ValueError, TypeError):
        return json.dumps({"cmd": "getStatus"})


def ws_binary_cases(rng):
    frames = [b"", b"\x03", b"\x03\x00", b"\x03\x00\x00\x00\x00", b"\x03\x00\x00\x00\x00\x00",
              b"\x03\x00\x00\x00\x00\x00\x01", b"\x03\x00\x00\x00\x00\x00\x01\x02", b"\x03\x00\x00\x00\x00\x00\x01\x02\x03",
              b"\x03\x01" + b"\x00" * 4 + b"\x7f\xff" * 5, b"\x03\xff" + b"\xff" * 4 + b"\x01" * 9601,
              b"\x03\x00\xff\xff\xff\xff" + bytes(rng.getrandbits(8) for _ in range(9600)),
              b"\x03\x00\x00\x00\x00\x00" + bytes(rng.getrandbits(8) for _ in range(9601)),
              b"\x03\x00" + b"\x00" * 4 + b"\x80\x00" * 100000,
              b"\x03\x00" + b"\x00" * 4 + b"\x00" * (4 * MB + 1),
              b"\x02\x00\x00\x00\x00\x00" + b"\x00" * 100, b"\x04\x00\x00\x00\x00\x00" + b"\x00" * 101,
              b"\x00" * 1000, b"\xff" * 1000, bytes(range(256))]
    for t in range(256):
        frames.append(bytes([t, rng.getrandbits(8)]) + bytes(rng.getrandbits(8) for _ in range(rng.randint(0, 40))))
    return frames


def ws_session(port, cookie=None, timeout=5.0):
    from websockets.sync.client import connect
    headers = {"Cookie": f"wfweb_session={cookie}"} if cookie else None
    return connect(f"wss://127.0.0.1:{port}", ssl=_TLS, open_timeout=timeout, close_timeout=2,
                   max_size=None, additional_headers=headers)


def ws_drain(ws, timeout=0.05):
    """Read what the server has sent so far (state, denials) and drop it.
    Polled with recv(timeout=0), which never blocks: a blocking
    recv(timeout) was seen to hang for good inside the websockets package
    (sync client, Python 3.14) with the server alive and answering."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            ws.recv(timeout=0)
        except TimeoutError:
            time.sleep(0.005)
        except Exception:
            return


def ws_text_batch(port, msgs, cookie=None, drain=0.3):
    """Send msgs on one socket, as text when str, binary when bytes.  A
    socket the server closes mid-way is reopened: a visitor on a --shared
    server gets 4401 at once and the rest must still be tried."""
    sent = 0
    while sent < len(msgs):
        try:
            with ws_session(port, cookie) as ws:
                ws_drain(ws, drain)
                while sent < len(msgs):
                    m = msgs[sent]
                    sent += 1
                    ws.send(m)
                    if sent % 20 == 0:
                        ws_drain(ws)
                ws_drain(ws, drain)
        except Exception as e:      # closed on us: carry on with the rest
            if sent == 0:
                return f"connect failed: {e!r}"
            sent += 1 if sent < len(msgs) else 0
    return None


# Raw RFC 6455 frames, for Qt's own parser: nothing the websockets package
# would send.
def raw_frame(opcode, payload, fin=True, rsv=0, mask=True, length=None):
    b0 = (0x80 if fin else 0) | (rsv << 4) | opcode
    n = len(payload) if length is None else length
    if n < 126:
        hdr = bytes([b0, (0x80 if mask else 0) | n])
    elif n < 65536:
        hdr = bytes([b0, (0x80 if mask else 0) | 126]) + struct.pack(">H", n)
    else:
        hdr = bytes([b0, (0x80 if mask else 0) | 127]) + struct.pack(">Q", n)
    if mask:
        key = b"\x12\x34\x56\x78"
        payload = bytes(c ^ key[i % 4] for i, c in enumerate(payload))
        hdr += key
    return hdr + payload


def raw_ws_cases(rng):
    text = json.dumps({"cmd": "getStatus"}).encode()
    cases = [
        ("unmasked text", raw_frame(1, text, mask=False)),
        ("rsv bits", raw_frame(1, text, rsv=7)),
        ("opcode 3", raw_frame(3, text)), ("opcode 7", raw_frame(7, text)), ("opcode 0xb", raw_frame(0xb, text)),
        ("opcode 0xf", raw_frame(0xf, text)),
        ("continuation first", raw_frame(0, text)),
        ("text not fin then text", raw_frame(1, text, fin=False) + raw_frame(1, text)),
        ("text not fin then cont", raw_frame(1, text[:5], fin=False) + raw_frame(0, text[5:])),
        ("fragmented 1000", b"".join([raw_frame(1, b"", fin=False)] + [raw_frame(0, b"x", fin=False) for _ in range(1000)] + [raw_frame(0, b"")])),
        ("ping 126", raw_frame(9, b"p" * 126)), ("ping 125", raw_frame(9, b"p" * 125)), ("ping not fin", raw_frame(9, b"p", fin=False)),
        ("pong", raw_frame(10, b"x")), ("close 1 byte", raw_frame(8, b"\x03")), ("close bad code", raw_frame(8, b"\x00\x00")),
        ("close 1005", raw_frame(8, struct.pack(">H", 1005))), ("close long reason", raw_frame(8, struct.pack(">H", 1000) + b"r" * 123)),
        ("close 126 payload", raw_frame(8, b"\x03\xe8" + b"r" * 124)),
        ("close bad utf8", raw_frame(8, b"\x03\xe8\xff\xfe")),
        ("text bad utf8", raw_frame(1, b"\xff\xfe\xc0\xaf")), ("text overlong", raw_frame(1, b"\xc0\x80")),
        ("text surrogate", raw_frame(1, b"\xed\xa0\x80")), ("text nul", raw_frame(1, b"\x00{\"cmd\":\"getStatus\"}")),
        ("len 126 short", raw_frame(1, b"x" * 10, length=126)), ("len 127 short", raw_frame(1, b"x" * 10, length=70000)),
        ("len 126 under 126", bytes([0x81, 0x80 | 126]) + struct.pack(">H", 5) + b"\x00" * 4 + b"hello"),
        ("len 127 under 65536", bytes([0x81, 0x80 | 127]) + struct.pack(">Q", 5) + b"\x00" * 4 + b"hello"),
        ("len 127 msb set", bytes([0x81, 0x80 | 127]) + struct.pack(">Q", 1 << 63) + b"\x00" * 4 + b"hello"),
        ("len 127 2^62", bytes([0x81, 0x80 | 127]) + struct.pack(">Q", 1 << 62) + b"\x00" * 4 + b"hello"),
        ("len 127 INT_MAX", bytes([0x81, 0x80 | 127]) + struct.pack(">Q", 2 ** 31 - 1) + b"\x00" * 4 + b"hello" * 1000),
        ("len 127 INT_MAX-1", bytes([0x81, 0x80 | 127]) + struct.pack(">Q", 2 ** 31 - 2) + b"\x00" * 4 + b"hello" * 1000),
        ("len 127 4G", bytes([0x81, 0x80 | 127]) + struct.pack(">Q", 2 ** 32) + b"\x00" * 4 + b"hello"),
        ("binary 1 MB", raw_frame(2, b"\x03\x00" + b"\x00" * MB)),
        ("binary 20 MB", raw_frame(2, b"\x03\x00" + b"\x00" * (20 * MB))),
        ("text 20 MB", raw_frame(1, b"{\"cmd\":\"getStatus\",\"x\":\"" + b"x" * (20 * MB) + b"\"}")),
        ("header only 1 byte", b"\x81"), ("header only 2 bytes", b"\x81\x85"), ("mask key short", b"\x81\x85\x12\x34"),
        ("random 4k", bytes(rng.getrandbits(8) for _ in range(4096))),
        ("many empty frames", raw_frame(1, b"") * 10000),
        ("many pings", raw_frame(9, b"") * 10000),
        ("many closes", raw_frame(8, b"\x03\xe8") * 100),
    ]
    return cases


def raw_ws_handshake(port, cookie=None, timeout=5.0):
    s = tls_connect(port, timeout)
    lines = [f"GET / HTTP/1.1", f"Host: 127.0.0.1:{port}", "Upgrade: websocket", "Connection: Upgrade",
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", "Sec-WebSocket-Version: 13"]
    if cookie:
        lines.append(f"Cookie: wfweb_session={cookie}")
    s.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
    s.settimeout(timeout)
    head = b""
    while b"\r\n\r\n" not in head:
        try:
            chunk = s.recv(4096)
        except ssl.SSLWantReadError:
            continue
        if not chunk:
            break
        head += chunk
    if not head.startswith(b"HTTP/1.1 101"):
        s.close()
        raise OSError(f"no upgrade: {head[:80]!r}")
    return s


def send_raw_ws(port, frames, cookie=None, read=True, timeout=2.0):
    try:
        s = raw_ws_handshake(port, cookie)
    except OSError as e:
        return b"<handshake failed: %r>" % (e,)
    try:
        try:
            s.sendall(frames)
        except OSError:
            pass
        return read_all(s, timeout, limit=65536) if read else b""
    finally:
        s.close()


# --- the run ------------------------------------------------------------------

def describe(data):
    if isinstance(data, bytes):
        if len(data) > 4096:
            return {"len": len(data), "head_b64": base64.b64encode(data[:2048]).decode(),
                    "tail_b64": base64.b64encode(data[-512:]).decode()}
        return {"len": len(data), "b64": base64.b64encode(data).decode(), "repr": repr(data)[:600]}
    return {"text": data if len(data) < 4096 else data[:2048] + f"... ({len(data)} chars)"}


class Fuzzer:
    def __init__(self, port, *, shared=False, seed=0, pid=None, cookie=None, report=None, verbose=False,
                 proc=None, check=None, quick=False):
        self.port = port
        # quick: the CI shape -- targeted HTTP cases in batches of 20 to one
        # side each (TLS / plain alternating) instead of one batch per case
        # to both, and a sample of the field x value grid per command.  The
        # same cases, fewer health checks: about a minute a side.
        self.quick = quick
        self.drain = 0.1 if quick else 0.3     # how long a WS batch listens before / after sending
        self.shared = shared
        self.seed = seed
        self.rng = random.Random(seed)
        self.pid = pid
        self.cookie = cookie
        self.report = report
        self.verbose = verbose
        self.proc = proc
        self.check = check        # extra callable -> error string or None (sanitizer log)
        self.findings = []
        self.cases = 0
        self.batches = 0
        self.notes = []

    def log(self, *a):
        print(*a, file=sys.stderr, flush=True)

    def _alive(self):
        if self.proc is not None and self.proc.poll() is not None:
            return f"process exited with {self.proc.returncode}"
        if not pid_alive(self.pid):
            return "process gone"
        return None

    def after_batch(self, label, inputs):
        self.batches += 1
        dead = self._alive()
        if dead is None and not health(self.port):
            dead = self._alive() or f"not answering GET /api/v1/radio/info within {HEALTH_TIMEOUT:.0f} s (hang?)"
        if dead is None and self.check:
            dead = self.check()
        if dead:
            f = {"batch": label, "seed": self.seed, "effect": dead,
                 "inputs": [{"label": l, "input": describe(d)} for l, d in inputs]}
            self.findings.append(f)
            self.log(f"!!! FINDING after batch {label!r}: {dead}")
            for l, d in inputs:
                self.log(f"    {l}: {describe(d).get('repr', describe(d).get('text', describe(d)))}")
            return False
        if self.verbose:
            self.log(f"ok  batch {label} ({len(inputs)} cases, {self.cases} total)")
        return True

    # Every case is its own batch when it is targeted (so a crash names its
    # input) and batches of 25 for random ones (speed).
    def run_http_targeted(self):
        batch = []
        cases = targeted_http_cases(self.rng, self.shared, self.quick)
        for i, (label, data, kw) in enumerate(cases):
            sides = ((i % 2 == 0),) if self.quick else (True, False)
            # A request the server (rightly) leaves unanswered -- a body
            # shorter than its Content-Length -- is waited on for 3 s, or
            # 0.5 s in the quick shape; an answer takes milliseconds.
            timeout = kw.get("timeout", 3.0)
            if self.quick:
                timeout = min(timeout, 0.5)
            for tls in sides:
                send_raw(self.port if tls else self.port + 1, data, tls=tls, pieces=kw.get("pieces"),
                         gap=kw.get("gap", 0.0), timeout=timeout)
                self.cases += 1
            batch.append((label, data))
            if len(batch) >= (20 if self.quick else 1) or i == len(cases) - 1:
                if not self.after_batch(f"http:{batch[0][0]}" + (f" (+{len(batch) - 1})" if len(batch) > 1 else ""), batch):
                    return False
                batch = []
        return True

    def run_http_random(self, n):
        batch = []
        for i in range(n):
            label, data = random_http_case(self.rng)
            tls = self.rng.random() < 0.5
            send_raw(self.port if tls else self.port + 1, data, tls=tls, timeout=2.0)
            self.cases += 1
            batch.append((label, data))
            if len(batch) == 25 or i == n - 1:
                if not self.after_batch(f"http:random#{self.batches}", batch):
                    return False
                batch = []
        return True

    def run_http_pressure(self):
        """Many requests on one connection without reading the answers, and a
        burst of connections asking for the biggest file: the write side."""
        try:
            s = tls_connect(self.port)
            for _ in range(200):
                s.sendall(req(path="/index.html"))
            time.sleep(1.0)
            s.close()
        except OSError:
            pass
        socks = []
        try:
            for _ in range(60):
                try:
                    s = tls_connect(self.port, 2.0)
                    s.sendall(req(path="/models/deepcw.onnx"))
                    socks.append(s)
                except OSError:
                    break
            time.sleep(1.0)
        finally:
            for s in socks:
                s.close()
        self.cases += 2
        rss = self.rss_mb()
        if rss:
            self.notes.append(f"RSS after write-pressure: {rss:.0f} MB")
        return self.after_batch("http:pressure", [("200 unread requests on one socket", b""), ("60 x 27 MB", b"")])

    def rss_mb(self):
        if not self.pid:
            return None
        try:
            with open(f"/proc/{self.pid}/status") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        return int(line.split()[1]) / 1024.0
        except OSError:
            return None
        return None

    def run_ws_targeted(self):
        try:
            import websockets  # noqa: F401
        except ImportError:
            self.log("websockets package missing: WebSocket text cases skipped")
            return True
        # Text: the odd JSON list, each as its own batch (quick: tens).
        per = 10 if self.quick else 1
        for i in range(0, len(WS_RAW_TEXT), per):
            chunk = WS_RAW_TEXT[i:i + per]
            err = ws_text_batch(self.port, chunk, self.cookie, drain=self.drain)
            self.cases += len(chunk)
            if err:
                self.notes.append(f"ws text {chunk[0][:40]!r}: {err}")
            if not self.after_batch(f"ws:text:{chunk[0][:40]!r}", [("text", t) for t in chunk]):
                return False
        # Every command with odd values in its fields, 25 per batch: the
        # whole 20 fields x 6 values grid, or a 4 x 3 sample of it (quick).
        pool = WS_COMMANDS if self.shared else [c for c in WS_COMMANDS if c not in WS_SKIP_PLAIN]
        batch = []
        n_fields, n_values = (4, 3) if self.quick else (20, 6)
        for cmd in pool:
            for field in self.rng.sample(WS_FIELDS[:20], n_fields):
                for v in self.rng.sample(WS_VALUES, n_values):
                    try:
                        batch.append(("cmd", json.dumps({"cmd": cmd, field: v})))
                    except (ValueError, TypeError):
                        continue
                    if len(batch) == 25:
                        ws_text_batch(self.port, [b for _, b in batch], self.cookie, drain=self.drain)
                        self.cases += len(batch)
                        if not self.after_batch(f"ws:fields:{cmd}", batch):
                            return False
                        batch = []
        if batch:
            ws_text_batch(self.port, [b for _, b in batch], self.cookie, drain=self.drain)
            self.cases += len(batch)
            if not self.after_batch("ws:fields:tail", batch):
                return False
        # Binary frames through the library (well-formed WS, odd content).
        frames = ws_binary_cases(self.rng)
        for i in range(0, len(frames), 25):
            chunk = frames[i:i + 25]
            ws_text_batch(self.port, chunk, self.cookie, drain=self.drain)
            self.cases += len(chunk)
            if not self.after_batch(f"ws:binary#{i // 25}", [("binary", f) for f in chunk]):
                return False
        # Raw frames at Qt's parser (quick: five per health check).
        raw = raw_ws_cases(self.rng)
        per = 5 if self.quick else 1
        for i in range(0, len(raw), per):
            chunk = raw[i:i + per]
            for label, data in chunk:
                send_raw_ws(self.port, data, self.cookie)
                self.cases += 1
            if not self.after_batch(f"ws:raw:{chunk[0][0]}" + (f" (+{len(chunk) - 1})" if len(chunk) > 1 else ""), chunk):
                return False
        # A half-open binary frame: a declared 100 MB that never arrives,
        # then the connection dropped.
        try:
            s = raw_ws_handshake(self.port, self.cookie)
            s.sendall(bytes([0x82, 0x80 | 127]) + struct.pack(">Q", 100 * MB) + b"\x00" * 4 + b"\x03\x00" * 1000)
            time.sleep(0.5)
            s.close()
        except OSError:
            pass
        self.cases += 1
        return self.after_batch("ws:raw:half-open 100 MB", [("half-open", b"")])

    def run_ws_random(self, n):
        try:
            import websockets  # noqa: F401
        except ImportError:
            return True
        batch = []
        for i in range(n):
            r = self.rng.random()
            if r < 0.6:
                m = random_ws_command(self.rng, self.shared)
            elif r < 0.8:
                m = mutate(self.rng, random_ws_command(self.rng, self.shared).encode()).decode("utf-8", "replace")
            else:
                m = bytes([self.rng.choice([0x03, 0x03, 0x02, 0x04, self.rng.getrandbits(8)]), self.rng.getrandbits(8)]) \
                    + bytes(self.rng.getrandbits(8) for _ in range(self.rng.choice([0, 1, 3, 4, 5, 6, 7, 9, 100, 1001, 9600])))
            batch.append(("random", m))
            self.cases += 1
            if len(batch) == 25 or i == n - 1:
                ws_text_batch(self.port, [b for _, b in batch], self.cookie, drain=self.drain)
                if not self.after_batch(f"ws:random#{self.batches}", batch):
                    return False
                batch = []
        return True

    def run(self, side="all", iterations=200, duration=None):
        """Targeted cases once, then random ones: iterations per side, or
        until duration seconds have passed.  True when nothing was found."""
        start = time.monotonic()
        # The websockets package's reader thread prints a traceback when
        # the server closes a socket under it (EOFError / reset): expected
        # here, hundreds of times.
        import threading
        default_hook = threading.excepthook
        threading.excepthook = (lambda a: None if issubclass(a.exc_type, (EOFError, ConnectionError, OSError))
                                else default_hook(a))
        if not health(self.port):
            self.findings.append({"batch": "start", "effect": "server not answering before the run", "inputs": []})
            return False
        ok = True
        if side in ("http", "all"):
            ok = self.run_http_targeted() and self.run_http_pressure()
        if ok and side in ("ws", "all"):
            ok = self.run_ws_targeted()
        round_ = 0
        while ok:
            round_ += 1
            if duration is None:
                if round_ > 1:
                    break
                n = iterations
            else:
                if time.monotonic() - start >= duration:
                    break
                n = 200
            if side in ("http", "all"):
                ok = self.run_http_random(n)
            if ok and side in ("ws", "all"):
                ok = self.run_ws_random(n)
            if duration is not None:
                self.log(f"... {time.monotonic() - start:.0f} s, {self.cases} cases, {self.batches} batches, "
                         f"RSS {self.rss_mb() or 0:.0f} MB")
        self.elapsed = time.monotonic() - start
        if self.report:
            with open(self.report, "w") as f:
                json.dump({"seed": self.seed, "port": self.port, "shared": self.shared, "cases": self.cases,
                           "batches": self.batches, "elapsed_s": round(self.elapsed, 1), "findings": self.findings,
                           "notes": self.notes}, f, indent=1)
        return not self.findings


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, required=True, help="HTTPS/WSS port (plain REST is port + 1)")
    ap.add_argument("--shared", action="store_true", help="the server runs with --shared")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--iterations", type=int, default=200, help="random cases per side after the targeted ones")
    ap.add_argument("--duration", type=float, default=None, help="long mode: random cases for this many seconds")
    ap.add_argument("--side", choices=["http", "ws", "all"], default="all")
    ap.add_argument("--pid", type=int, default=None, help="server pid, to tell a crash from a hang")
    ap.add_argument("--cookie", default=None, help="a wfweb_session value for the WebSocket side")
    ap.add_argument("--report", default=None, help="write a JSON report here")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--quick", action="store_true", help="the CI shape: batched targeted cases, a sample of the field grid")
    a = ap.parse_args(argv)
    fz = Fuzzer(a.port, shared=a.shared, seed=a.seed, pid=a.pid, cookie=a.cookie, report=a.report, verbose=a.verbose,
                quick=a.quick)
    ok = fz.run(a.side, a.iterations, a.duration)
    print(f"{fz.cases} cases in {fz.batches} batches, {fz.elapsed:.0f} s, seed {a.seed}: "
          f"{'no findings' if ok else str(len(fz.findings)) + ' finding(s)'}", file=sys.stderr)
    for n in fz.notes:
        print("note:", n, file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
