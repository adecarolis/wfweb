"""WASAPI loopback capture of a Windows render endpoint + gap analysis.

  python -I loopback.py rec  --device "USB Audio CODEC" --seconds 12 --out cap.wav
  python -I loopback.py analyze cap.wav [--tone 1000]

Records what Windows actually sends to the named playback endpoint (post-mix),
without touching the radio.  The analysis slices the file into 5 ms blocks,
reports level per block, the active window, every gap inside it, and the
dominant frequency, so a steady test tone fed through wfweb's TX path shows
dropouts as gaps.
"""
import argparse
import sys
import wave

import numpy as np


def list_devices(p):
    import pyaudiowpatch as pa
    print("Loopback-capable render endpoints:")
    for d in p.get_loopback_device_info_generator():
        print(f"  [{d['index']}] {d['name']}  rate={int(d['defaultSampleRate'])} ch={d['maxInputChannels']}")


def rec(args):
    import pyaudiowpatch as pa
    p = pa.PyAudio()
    try:
        if args.list:
            list_devices(p)
            return 0
        target = None
        for d in p.get_loopback_device_info_generator():
            if args.device.lower() in d["name"].lower():
                target = d
                break
        if target is None:
            print(f"no loopback device matching {args.device!r}")
            list_devices(p)
            return 2
        rate = int(target["defaultSampleRate"])
        ch = int(target["maxInputChannels"])
        # WASAPI loopback only yields packets while the endpoint is rendering.
        # Keep it running with a silent output stream for the whole capture so
        # the timeline is continuous and a starved wfweb stream shows as zeros.
        render = None
        base = target["name"].replace(" [Loopback]", "")
        for i in range(p.get_device_count()):
            d = p.get_device_info_by_index(i)
            if d["name"] == base and d["maxOutputChannels"] > 0 and \
                    p.get_host_api_info_by_index(d["hostApi"])["type"] == pa.paWASAPI:
                render = d
                break
        print(f"recording [{target['index']}] {target['name']} rate={rate} ch={ch} for {args.seconds}s -> {args.out}"
              f" (silence keeper on [{render['index'] if render else None}])", flush=True)
        frames = []

        def on_in(data, n, t, status):
            frames.append(data)
            return (None, pa.paContinue)

        zeros = b"\0" * (1024 * 2 * 2)

        def on_out(data, n, t, status):
            return (zeros[: n * 2 * 2], pa.paContinue)

        keeper = None
        if render is not None:
            keeper = p.open(format=pa.paInt16, channels=2, rate=rate, output=True,
                            output_device_index=render["index"], frames_per_buffer=1024,
                            stream_callback=on_out)
            keeper.start_stream()
        stream = p.open(format=pa.paInt16, channels=ch, rate=rate, input=True,
                        input_device_index=target["index"], frames_per_buffer=1024,
                        stream_callback=on_in)
        stream.start_stream()
        import time
        t0 = time.time()
        while time.time() - t0 < args.seconds:
            time.sleep(0.05)
        stream.stop_stream()
        stream.close()
        if keeper:
            keeper.stop_stream()
            keeper.close()
        print(f"captured {sum(len(f) for f in frames) / (2 * ch) / rate:.2f}s of audio", flush=True)
        with wave.open(args.out, "wb") as w:
            w.setnchannels(ch)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(b"".join(frames))
        print("done", flush=True)
    finally:
        p.terminate()
    return analyze_file(args.out, args.tone)


def analyze(args):
    return analyze_file(args.file, args.tone)


def analyze_file(path, tone=None):
    with wave.open(path, "rb") as w:
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    x = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    dur = len(x) / rate
    blk = int(rate * 0.005)  # 5 ms
    nblk = len(x) // blk
    rms = np.sqrt(np.mean(x[: nblk * blk].reshape(nblk, blk) ** 2, axis=1) + 1e-12)
    db = 20 * np.log10(rms + 1e-9)
    ACTIVE, GAP = -45.0, -60.0
    act = db > ACTIVE
    print(f"file={path} rate={rate} ch={ch} dur={dur:.2f}s blocks(5ms)={nblk}")
    print(f"peak={20*np.log10(np.max(np.abs(x))+1e-9):.1f} dBFS  overall rms={20*np.log10(np.sqrt(np.mean(x**2))+1e-9):.1f} dBFS")
    if not act.any():
        print("NO SIGNAL above -45 dBFS anywhere: the endpoint received silence")
        return 1
    first, last = int(np.argmax(act)), int(len(act) - np.argmax(act[::-1]) - 1)
    print(f"active window: {first*0.005:.3f}s .. {(last+1)*0.005:.3f}s  ({(last-first+1)*0.005:.3f}s)")
    inner = db[first : last + 1]
    gaps = []
    i = 0
    while i < len(inner):
        if inner[i] < GAP:
            j = i
            while j < len(inner) and inner[j] < GAP:
                j += 1
            gaps.append(((first + i) * 0.005, (j - i) * 5))
            i = j
        else:
            i += 1
    print(f"gaps (<{GAP:.0f} dBFS) inside the active window: {len(gaps)}")
    for t, ms in gaps[:40]:
        print(f"   at {t:8.3f}s  {ms:5d} ms")
    if len(gaps) > 40:
        print(f"   ... {len(gaps)-40} more")
    med = float(np.median(inner[inner > GAP])) if (inner > GAP).any() else float("nan")
    lo = int(np.sum((inner > GAP) & (inner < med - 6)))
    print(f"median active level {med:.1f} dBFS; blocks more than 6 dB under it: {lo} ({lo*5} ms)")
    # one char per 100 ms
    line = []
    per = 20
    for k in range(0, nblk, per):
        seg = act[k : k + per]
        f = seg.mean()
        line.append("#" if f > 0.95 else ("-" if f > 0.05 else "."))
    print("timeline (100 ms/char, #=solid -=partial .=silent):")
    s = "".join(line)
    for k in range(0, len(s), 100):
        print(f"  {k/10:5.1f}s {s[k:k+100]}")
    seg = x[first * blk : (last + 1) * blk]
    if len(seg) > rate // 4:
        win = seg[: min(len(seg), rate * 2)]
        spec = np.abs(np.fft.rfft(win * np.hanning(len(win))))
        freqs = np.fft.rfftfreq(len(win), 1 / rate)
        k = int(np.argmax(spec[1:]) + 1)
        print(f"dominant frequency in first 2 s of the active window: {freqs[k]:.1f} Hz")
        if tone:
            print(f"expected tone {tone} Hz -> {'OK' if abs(freqs[k]-tone) < 5 else 'MISMATCH'}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("rec")
    r.add_argument("--device", default="USB Audio CODEC")
    r.add_argument("--seconds", type=float, default=10)
    r.add_argument("--out", default="cap.wav")
    r.add_argument("--tone", type=float, default=None)
    r.add_argument("--list", action="store_true")
    r.set_defaults(fn=rec)
    a = sub.add_parser("analyze")
    a.add_argument("file")
    a.add_argument("--tone", type=float, default=None)
    a.set_defaults(fn=analyze)
    args = ap.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
