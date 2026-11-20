#!/usr/bin/env python3
"""RADE over LAN, on the two-rig bench (issue #158).

The voice modems hand a LAN rig their audio through a pacer (20 ms slices
in real time, `modemLanTxQueue` in webserver.cpp).  This drives one RADE
over from rig A with real speech as the mic and checks, at rig B:

  1. B on plain SSB: the modem signal arrives whole, no quiet 20 ms window
     in the voice part of the over, and as long as the over was;
  2. B in RADE: the callsign of the end-of-over frame is decoded, so the
     frame went out complete before the key dropped.

The virtual rig takes TX audio in any shape, so this cannot show what a
real IC-705 does with a burst; it shows the paced audio is intact and in
real time.  A's log has the pacer's own account at the end of each mic
session ("LAN modem TX - 15.0 s sent in 751 slices, encoder ran dry 0
time(s)").

    scripts/testrig.sh up 2
    uv run --with websockets --with numpy tests/rade_lan_bench.py
    scripts/testrig.sh down

Speech and not a tone: RADE's modem syncs poorly on synthetic audio.
"""
import argparse, json, math, os, ssl, struct, sys, threading, time
import numpy as np
from websockets.sync.client import connect

RATE = 48000
HERE = os.path.dirname(os.path.abspath(__file__))

ap = argparse.ArgumentParser()
ap.add_argument('--a', type=int, default=9080, help='web port of the transmitting rig')
ap.add_argument('--b', type=int, default=9090, help='web port of the receiving rig')
ap.add_argument('--seconds', type=float, default=12.0)
ap.add_argument('--call', default='W1AAA')
ap.add_argument('--wav', default=os.path.join(HERE, '..', 'resources', 'radae_nopy', 'wav', 'peter.wav'))
args = ap.parse_args()


def tls():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2    # as ws_tls_context() in conftest.py
    return ctx


def load_speech(path):
    import wave
    w = wave.open(path, 'rb')
    raw = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
    if w.getnchannels() == 2:
        raw = raw[::2]
    x = np.arange(0, len(raw) - 1, w.getframerate() / RATE)
    s = np.interp(x, np.arange(len(raw)), raw)
    return s / max(1.0, np.abs(s).max()) * 0.8 * 32767


SPEECH = load_speech(args.wav)


def mic_frame(seq):
    n = RATE // 50
    idx = (seq * n + np.arange(n)) % len(SPEECH)
    return struct.pack('<BBHH', 0x03, 0x00, seq & 0xffff, 0) + SPEECH[idx].astype('<i2').tobytes()


class Rig:
    def __init__(self, port):
        self.ws = connect(f"wss://127.0.0.1:{port}", ssl=tls(), open_timeout=10, max_size=None)
        self.audio = []          # (monotonic time, int16 array) of 0x02 frames
        self.calls = []          # RADE callsigns decoded
        self.stop = threading.Event()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while not self.stop.is_set():
            try:
                m = self.ws.recv(timeout=0.5)
            except TimeoutError:
                continue
            except Exception:
                return
            if isinstance(m, (bytes, bytearray)):
                if len(m) > 6 and m[0] == 0x02:
                    self.audio.append((time.monotonic(), np.frombuffer(m[6:], dtype='<i2').copy()))
            else:
                try:
                    j = json.loads(m)
                except ValueError:
                    continue
                if j.get('type') == 'radeCallsign' and j.get('callsign'):
                    self.calls.append(j['callsign'])

    def send(self, **cmd):
        self.ws.send(json.dumps(cmd))


def one_over(a, seconds):
    """Key A for `seconds` with the mic streaming in real time, as the page
    does: before the key, through the over, and on after the release."""
    a.send(cmd='enableMic', value=True, device='bench')
    start = time.monotonic()
    seq = 0

    def stream(until):
        nonlocal seq
        while time.monotonic() < until:
            a.ws.send(mic_frame(seq))
            seq += 1
            d = start + seq * 0.020 - time.monotonic()
            if d > 0:
                time.sleep(d)

    stream(start + 1.0)
    t_key = time.monotonic()
    a.send(cmd='setPTT', value=True)
    stream(t_key + seconds)
    t_unkey = time.monotonic()
    a.send(cmd='setPTT', value=False)
    stream(t_unkey + 1.5)
    a.send(cmd='enableMic', value=False)
    return t_key, t_unkey


fails = 0


def check(ok, msg):
    global fails
    print(('PASS ' if ok else 'FAIL ') + msg)
    if not ok:
        fails += 1


a, b = Rig(args.a), Rig(args.b)
a.send(cmd='setStationCallsign', callsign=args.call, grid='FN42')
a.send(cmd='setFreeDV', enabled=True, mode='RADE')
b.send(cmd='setFreeDV', enabled=False)
b.send(cmd='enableAudio', value=True)
time.sleep(2.0)

# ---- 1. the modem signal as B's receiver hears it
b.audio.clear()
time.sleep(0.5)
t_key, t_unkey = one_over(a, args.seconds)
time.sleep(1.0)
frames = list(b.audio)
pcm = np.concatenate([p for _, p in frames]).astype(np.float64) / 32768.0
t_end = frames[-1][0]


def at(t):                      # sample index of a monotonic time
    return int(max(0, min(len(pcm), len(pcm) - (t_end - t) * RATE)))


win = RATE // 50
rms = np.array([math.sqrt(np.mean(pcm[i:i + win] ** 2) + 1e-12) for i in range(0, len(pcm) - win, win)])
idle = rms[max(0, at(t_key) // win - 40): max(1, at(t_key) // win - 5)]
thr = max(float(np.median(idle)) * 4 if len(idle) else 0.0, 0.003)
k0, k1 = at(t_key) // win, at(t_unkey) // win
loud = np.where(rms[k0:] > thr)[0]
check(len(loud) > 0, "B hears A's over")
if len(loud):
    first = k0 + int(loud[0])
    last = k0 + int(loud[-1])
    # The voice part only: the end-of-over frame after the release has a
    # quiet stretch of its own (pilots, data slots, pilots).
    voice = rms[first:k1 + 1]
    quiet = int(np.sum(voice <= thr))
    check(quiet == 0, f"no quiet 20 ms window in {len(voice) * 0.02:.1f} s of modem signal "
                      f"(found {quiet}; rms median {np.median(voice):.4f}, min {voice.min():.4f})")
    heard = (last - first + 1) * 0.02
    check(abs(heard - (t_unkey - t_key)) < 1.0,
          f"over heard for {heard:.2f} s, keyed for {t_unkey - t_key:.2f} s")

# ---- 2. the end-of-over callsign, B in RADE
b.send(cmd='setFreeDV', enabled=True, mode='RADE')
time.sleep(2.0)
got = False
for attempt in range(3):
    b.calls.clear()
    one_over(a, args.seconds)
    time.sleep(1.5)
    if args.call in b.calls:
        got = True
        break
check(got, f"B decoded the end-of-over callsign {args.call} (attempt {attempt + 1} of 3)")

a.send(cmd='setFreeDV', enabled=False)
b.send(cmd='setFreeDV', enabled=False)
time.sleep(0.5)
a.stop.set(); b.stop.set()
sys.exit(1 if fails else 0)
