"""HTTP connection hygiene of the hand-rolled web server (src/webserver.cpp).

wfweb's HTTP side is its own parser on QTcpSocket / QSslSocket, written for
a browser on the LAN. Shared Station mode puts it on the internet, where the
first visitors are a slowloris, a connection flood and a page on somebody
else's site reaching for the WebSocket, so onHttpConnection /
sweepHttpConnections / httpRequestDone now bound what a connection may do:
its headers complete within kHttpHeaderTimeoutMs (10 s) of accept, its
response is drained within kHttpDrainStallMs, no more than
kHttpMaxConnections (128) sockets are kept at once, a WebSocket upgrade is
taken only from the page's own origin, and every response carries the
headers a browser expects (nosniff, no-referrer, no-store) -- with HSTS only
behind a certificate the browser trusts: pinned to wfweb's self-signed one it
would take away the user's "proceed anyway" and lock them out.

Everything here talks to the session's wfweb (conftest.wfweb_instance:
HTTPS + WSS on web_port, plain-HTTP REST on web_port + 1) through the
stdlib socket / ssl modules, so a hole in the parser is not papered over by
a client library's own hygiene. The slowloris sockets are cut at the first
sweep past 10 s (the sweep runs once a second), so that test takes ~11 s.
"""

import selectors
import socket
import ssl
import time
import warnings

import pytest
import requests

# Mirrors include/webserver.h; the assertions below are written against them.
HTTP_MAX_CONNECTIONS = 128      # kHttpMaxConnections
HTTP_HEADER_TIMEOUT_S = 10.0    # kHttpHeaderTimeoutMs
SWEEP_PERIOD_S = 1.0            # httpSweepTimer


# One context for the module: create_default_context() reads the system CA
# bundle every time, tens of ms per call, which the 148 handshakes of the cap
# test cannot afford against a 10 s header timeout. Nothing is verified
# anyway (wfweb's certificate is self-signed).
_TLS_CTX = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
_TLS_CTX.check_hostname = False
_TLS_CTX.verify_mode = ssl.CERT_NONE


def _tls_connect(port, timeout=10.0):
    """A TLS socket to wfweb's HTTPS port with the handshake done. Raises
    OSError (ssl.SSLError included) when the server will not have it."""
    raw = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    try:
        return _TLS_CTX.wrap_socket(raw)
    except BaseException:
        raw.close()
        raise


def _read_head(sock, timeout=10.0):
    """Whatever the server answers up to the end of its headers, or up to
    the close / timeout. Empty when it said nothing."""
    sock.settimeout(timeout)
    buf = b""
    deadline = time.monotonic() + timeout
    while b"\r\n\r\n" not in buf and time.monotonic() < deadline:
        try:
            chunk = sock.recv(4096)
        except ssl.SSLWantReadError:
            continue
        except OSError:          # timeout, reset, TLS EOF
            break
        if not chunk:
            break
        buf += chunk
    return buf


def _wait_closed(socks, limit_s):
    """When the server closes each of socks: {sock: time.monotonic()} for
    those it closed within limit_s, the others absent. A socket is probed
    without blocking when it turns readable: a close is an EOF or a reset,
    while a TLS record that carries no application data (TLS 1.3 hands out
    a session ticket right after the handshake) leaves recv wanting more
    and is not a close. selectors, not select: a pytest process may have
    fds past FD_SETSIZE."""
    closed = {}
    sel = selectors.DefaultSelector()
    for s in socks:
        sel.register(s, selectors.EVENT_READ)
    deadline = time.monotonic() + limit_s
    try:
        while len(closed) < len(socks):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            for key, _ in sel.select(remaining):
                s = key.fileobj
                s.settimeout(0.0)
                try:
                    data = s.recv(4096)
                except (ssl.SSLWantReadError, BlockingIOError, socket.timeout):
                    continue             # open: nothing for us behind that record
                except OSError:
                    data = b""           # reset: closed
                if data == b"":
                    closed[s] = time.monotonic()
                    sel.unregister(s)
                # Anything else: the server spoke; keep waiting for the close.
    finally:
        sel.close()
    return closed


def _ws_upgrade(web_port, origin, host=None):
    """A raw RFC 6455 opening handshake over TLS; returns the server's
    response head. origin None sends no Origin header, as a script does."""
    lines = [
        "GET / HTTP/1.1",
        f"Host: {host or f'127.0.0.1:{web_port}'}",
        "Upgrade: websocket",
        "Connection: Upgrade",
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==",
        "Sec-WebSocket-Version: 13",
    ]
    if origin is not None:
        lines.append(f"Origin: {origin}")
    s = _tls_connect(web_port)
    try:
        s.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
        return _read_head(s, timeout=5.0)
    finally:
        s.close()


def test_security_headers(wfweb_instance):
    """The SPA and the REST API both go out with nosniff and no-referrer; the
    API is no-store; and there is no HSTS on the self-signed certificate."""
    _, web_port = wfweb_instance

    with warnings.catch_warnings():
        warnings.simplefilter("ignore")    # verify=False on wfweb's own certificate
        r = requests.get(f"https://127.0.0.1:{web_port}/", verify=False, timeout=10)
    assert r.status_code == 200
    assert r.headers.get("X-Content-Type-Options") == "nosniff"
    assert r.headers.get("Referrer-Policy") == "no-referrer"
    assert "Strict-Transport-Security" not in r.headers, (
        "HSTS on a self-signed certificate locks the browser out of the page: "
        f"{r.headers['Strict-Transport-Security']!r}")

    r = requests.get(f"http://127.0.0.1:{web_port + 1}/api/v1/radio/info", timeout=10)
    assert r.status_code == 200
    assert r.headers.get("X-Content-Type-Options") == "nosniff"
    assert r.headers.get("Referrer-Policy") == "no-referrer"
    assert r.headers.get("Cache-Control") == "no-store"


def test_slow_headers_are_cut(wfweb_instance):
    """A request whose headers never complete, and a TLS client that says
    nothing at all, are both dropped at the first sweep past
    kHttpHeaderTimeoutMs from accept -- not kept for as long as the peer
    cares to hold them (slowloris). The clock runs from our connect, which
    is the server's accept to within the handshake."""
    _, web_port = wfweb_instance
    opened = {}
    partial = silent = None
    try:
        t = time.monotonic()
        partial = _tls_connect(web_port)
        opened[partial] = t
        t = time.monotonic()
        silent = _tls_connect(web_port)
        opened[silent] = t
        partial.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n")       # no blank line, ever

        closed = _wait_closed([partial, silent], HTTP_HEADER_TIMEOUT_S + 5.0)
        for name, s in (("partial headers", partial), ("silent", silent)):
            assert s in closed, f"{name}: still open 15 s after connect"
            elapsed = closed[s] - opened[s]
            assert 8.0 <= elapsed <= 15.0, (
                f"{name}: closed after {elapsed:.1f} s, expected the sweep after "
                f"{HTTP_HEADER_TIMEOUT_S:.0f} s (within {SWEEP_PERIOD_S:.0f} s)")
    finally:
        for s in (partial, silent):
            if s is not None:
                s.close()


def test_paused_request_completes(wfweb_instance):
    """The timeout is on the headers as a whole, from accept, not between
    bytes: a request that pauses 3 s after its request line (a phone on a
    poor link) still gets its 200 as long as it completes within the 10 s."""
    _, web_port = wfweb_instance
    s = _tls_connect(web_port)
    try:
        s.sendall(b"GET / HTTP/1.1\r\n")
        time.sleep(3.0)
        s.sendall(f"Host: 127.0.0.1:{web_port}\r\nConnection: close\r\n\r\n".encode())
        head = _read_head(s)
    finally:
        s.close()
    assert head.startswith(b"HTTP/1.1 200 "), head[:200]


def test_cross_site_websocket_refused(wfweb_instance):
    """A browser always sends Origin on a WebSocket upgrade: one naming
    another site is refused with 403 (a page there could otherwise drive
    the rig with this browser's cookies), one naming this host passes, and
    an upgrade without Origin (a script) passes. Hosts are compared without
    the port and case-insensitively."""
    _, web_port = wfweb_instance

    head = _ws_upgrade(web_port, origin="https://evil.example")
    assert head.startswith(b"HTTP/1.1 403"), head[:200]

    head = _ws_upgrade(web_port, origin=f"https://127.0.0.1:{web_port}")
    assert head.startswith(b"HTTP/1.1 101"), head[:200]

    head = _ws_upgrade(web_port, origin=None)
    assert head.startswith(b"HTTP/1.1 101"), head[:200]

    # Same host in another case and without the port: still the same site.
    head = _ws_upgrade(web_port, origin="https://localhost", host=f"LocalHost:{web_port}")
    assert head.startswith(b"HTTP/1.1 101"), head[:200]


def test_connection_cap(wfweb_instance):
    """More than kHttpMaxConnections sockets at once and the newcomers are
    abort()ed at accept -- their handshake, or the first read after it,
    fails at once while the earlier ones live on to the header timeout --
    and once the flood is gone the server answers as before. Counts rather
    than positions: the order the server sees is not quite ours."""
    try:
        import resource
    except ImportError:
        resource = None
    if resource is not None:
        soft, _ = resource.getrlimit(resource.RLIMIT_NOFILE)
        if soft < 256:
            pytest.skip(f"open-file limit {soft} too low to hold {HTTP_MAX_CONNECTIONS + 20} sockets")

    _, web_port = wfweb_instance
    total = HTTP_MAX_CONNECTIONS + 20
    held = []
    refused = []        # (index, seconds) of a handshake the server cut
    timed_out = []      # a handshake that hung: neither accepted nor refused
    try:
        t_loop = time.monotonic()
        for i in range(total):
            t = time.monotonic()
            try:
                held.append(_tls_connect(web_port, timeout=3.0))
            except socket.timeout:
                timed_out.append(i)
            except OSError:         # reset or TLS EOF: abort()ed at accept
                refused.append((i, time.monotonic() - t))
        loop_s = time.monotonic() - t_loop

        # Behind a TLS proxy (macOS) the handshake is the proxy's and the
        # refusal lands on the socket behind it: a held socket the server has
        # closed by now counts the same.
        closed_after = _wait_closed(held, 1.0)
        n_refused = len(refused) + len(closed_after)
        n_open = len(held) - len(closed_after)
        detail = (f"{total} connections in {loop_s:.1f} s: {len(refused)} refused in the "
                  f"handshake, {len(closed_after)} closed after it, {len(timed_out)} hung, "
                  f"{n_open} left open")
        print(detail)

        assert n_refused >= 1, f"nothing refused beyond {HTTP_MAX_CONNECTIONS} ({detail})"
        slow = [(i, s) for i, s in refused if s >= 3.0]
        assert not slow, f"refusals should be immediate, not {slow} ({detail})"
        if loop_s < HTTP_HEADER_TIMEOUT_S - SWEEP_PERIOD_S:
            # No sweep could have freed a slot meanwhile, so the cap is exact.
            assert n_open <= HTTP_MAX_CONNECTIONS, detail
    finally:
        for s in held:
            s.close()

    # The flood is gone: a fresh request is served (the server frees a slot
    # as each close reaches it, so allow it a moment).
    deadline = time.monotonic() + 10.0
    last = None
    while time.monotonic() < deadline:
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                r = requests.get(f"https://127.0.0.1:{web_port}/", verify=False, timeout=10)
            if r.status_code == 200:
                break
            last = f"HTTP {r.status_code}"
        except requests.RequestException as e:
            last = repr(e)
        time.sleep(0.2)
    else:
        pytest.fail(f"server not answering after the flood: {last}")
