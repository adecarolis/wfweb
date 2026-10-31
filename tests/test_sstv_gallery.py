"""SSTV image gallery on the server (src/webserver.cpp, /api/v1/sstv/images).

The browser decodes the pictures and uploads each as a PNG so every device
sees the same gallery: list newest first, fetch the PNG back byte for byte,
delete; a picture loaded to send is stored with kind=tx and its file name,
a received one is kind=rx (the default); a body that is not a PNG is
refused, one over 4 MB too, an id with anything but [A-Za-z0-9-] is
unknown; every store and delete is broadcast to
the WebSocket clients as {"type":"sstvGallery","count":N}.  Runs on the
session's wfweb (default --sstv-dir under the test HOME).
"""

import json
import struct
import time
import zlib

import pytest
import requests

from conftest import ws_connect


def _base(rest_url):
    return rest_url.replace("/api/v1/radio", "/api/v1/sstv/images")


def _png(w=4, h=3, color=(200, 30, 120)):
    """A valid RGB PNG built by hand: no Pillow needed."""
    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(color) * w for _ in range(h))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b""))


def _clear(base):
    for item in requests.get(base, timeout=5).json()["images"]:
        requests.delete(f"{base}/{item['id']}", timeout=5)


@pytest.fixture
def base(wfweb_instance):
    _, web_port = wfweb_instance
    b = _base(f"http://127.0.0.1:{web_port + 1}/api/v1/radio")
    _clear(b)
    yield b
    _clear(b)


def test_store_list_fetch_delete(base):
    empty = requests.get(base, timeout=5).json()
    assert empty["images"] == [] and empty["count"] == 0
    assert empty["dir"] and isinstance(empty["persistent"], bool)

    png = _png()
    r = requests.post(f"{base}?mode=Robot%2036&freq=14230000&call=k1fm&complete=1&w=320&h=240"
                      "&ts=2026-10-31T16:23:28.555Z",
                      data=png, headers={"Content-Type": "image/png"}, timeout=5)
    assert r.status_code == 200, r.text
    first = r.json()
    assert first["count"] == 1 and first["id"].startswith("20261031T162328Z-")

    time.sleep(1.1)
    r2 = requests.post(f"{base}?mode=PD-120&freq=7171000&complete=0&w=640&h=496",
                       data=png, headers={"Content-Type": "image/png"}, timeout=5)
    assert r2.status_code == 200
    second = r2.json()["id"]

    listing = requests.get(base, timeout=5).json()
    assert listing["count"] == 2
    ids = [i["id"] for i in listing["images"]]
    assert ids == [second, first["id"]], "newest first"
    item = listing["images"][1]
    assert item["mode"] == "Robot 36" and item["freqHz"] == 14230000 and item["call"] == "K1FM"
    assert item["complete"] is True and item["w"] == 320 and item["h"] == 240
    assert item["kind"] == "rx" and item["name"] == ""
    assert item["ts"].startswith("2026-10-31T16:23:28") and item["bytes"] == len(png)
    assert listing["images"][0]["complete"] is False

    got = requests.get(f"{base}/{first['id']}.png", timeout=5)
    assert got.status_code == 200
    assert got.headers["Content-Type"] == "image/png"
    assert "immutable" in got.headers.get("Cache-Control", "")
    assert got.content == png

    d = requests.delete(f"{base}/{first['id']}", timeout=5)
    assert d.status_code == 200 and d.json()["count"] == 1
    assert requests.get(f"{base}/{first['id']}.png", timeout=5).status_code == 404
    assert requests.delete(f"{base}/{first['id']}", timeout=5).status_code == 404
    assert [i["id"] for i in requests.get(base, timeout=5).json()["images"]] == [second]


def test_loaded_picture_kind(base):
    """A picture picked for TX is kept apart from the received ones."""
    r = requests.post(f"{base}?kind=tx&name=holiday%20snap.jpg&w=640&h=480&complete=1",
                      data=_png(), headers={"Content-Type": "image/png"}, timeout=5)
    assert r.status_code == 200
    item = requests.get(base, timeout=5).json()["images"][0]
    assert item["kind"] == "tx" and item["name"] == "holiday snap.jpg" and item["mode"] == ""
    time.sleep(1.1)                      # ids are second-resolution: a later second sorts first
    r = requests.post(f"{base}?kind=bogus&mode=Martin%201", data=_png(),
                      headers={"Content-Type": "image/png"}, timeout=5)
    assert r.status_code == 200
    assert requests.get(base, timeout=5).json()["images"][0]["kind"] == "rx", "an unknown kind is a received picture"


def test_refusals(base):
    r = requests.post(f"{base}?mode=X", data=b"GIF89a not a png", headers={"Content-Type": "image/png"}, timeout=5)
    assert r.status_code == 400
    big = _png() + b"\x00" * (4 * 1024 * 1024 + 1)
    r = requests.post(f"{base}?mode=X", data=big, headers={"Content-Type": "image/png"}, timeout=30)
    assert r.status_code == 413
    assert requests.get(base, timeout=5).json()["count"] == 0
    for bad in ("..%2Fx", "a.b", "x%00y", "z" * 65):
        assert requests.get(f"{base}/{bad}.png", timeout=5).status_code == 404, bad
        assert requests.delete(f"{base}/{bad}", timeout=5).status_code == 404, bad
    assert requests.put(base, data=b"", timeout=5).status_code == 405
    assert requests.get(f"{base}/notthere.png", timeout=5).status_code == 404


def test_oversize_request_is_refused_early(base):
    """The HTTP reader caps any request at 16 MB (there was no cap)."""
    r = requests.post(f"{base}?mode=X", data=b"\x89PNG\r\n\x1a\n" + b"\x00" * (17 * 1024 * 1024),
                      headers={"Content-Type": "image/png"}, timeout=60)
    assert r.status_code == 413


def test_change_is_broadcast(wfweb_instance, base):
    pytest.importorskip("websockets")
    proc, web_port = wfweb_instance
    ws = ws_connect(web_port, proc)
    try:
        # Drain the connect-time state messages.
        deadline = time.time() + 3
        while time.time() < deadline:
            try:
                ws.recv(timeout=0.5)
            except Exception:
                break
        r = requests.post(f"{base}?mode=Martin%201&w=320&h=256", data=_png(),
                          headers={"Content-Type": "image/png"}, timeout=5)
        assert r.status_code == 200
        seen = None
        deadline = time.time() + 5
        while time.time() < deadline and seen is None:
            try:
                msg = json.loads(ws.recv(timeout=1))
            except Exception:
                continue
            if msg.get("type") == "sstvGallery":
                seen = msg
        assert seen is not None and seen["count"] == 1, seen
    finally:
        ws.close()
