"""Replay the browser's mic TX stream against a running wfweb server.

  python txfeed.py --seconds 10 --tone 1000 [--burst 1] [--url wss://localhost:8080]

Opens the same WebSocket the SPA uses, sends {"cmd":"enableMic","value":true},
then streams type-0x03 frames of 128 int16 samples (256 bytes, 2.667 ms) at
wall-clock cadence, exactly like the AudioWorklet in index.html, carrying a
steady tone.  --burst N sends N frames back to back every N*2.667 ms, which
mimics a browser that batches worklet quanta.  No PTT is ever sent: the rig
stays in receive, only its USB codec playback endpoint gets the audio.
"""
import argparse
import asyncio
import json
import math
import ssl
import struct
import sys
import time

import websockets


def frames(seconds, tone, amp, rate=48000, n=128):
    total = int(seconds * rate)
    seq = 0
    pos = 0
    while pos < total:
        pcm = struct.pack("<%dh" % n, *[
            int(max(-32768, min(32767, round(amp * 32767 * math.sin(2 * math.pi * tone * (pos + i) / rate)))))
            for i in range(n)
        ])
        yield struct.pack("<BBHH", 0x03, 0x00, seq & 0xFFFF, 0) + pcm
        seq += 1
        pos += n


async def run(args):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    async with websockets.connect(args.url, ssl=ctx if args.url.startswith("wss") else None,
                                  max_size=None, ping_interval=None) as ws:
        rx = {"text": 0, "bin": 0, "txAudioAvailable": None, "warn": None}

        async def reader():
            try:
                async for m in ws:
                    if isinstance(m, (bytes, bytearray)):
                        rx["bin"] += 1
                    else:
                        rx["text"] += 1
                        try:
                            j = json.loads(m)
                            if "txAudioAvailable" in j:
                                rx["txAudioAvailable"] = j["txAudioAvailable"]
                            if j.get("type") == "micWarning":
                                rx["warn"] = j.get("message")
                        except Exception:
                            pass
            except Exception:
                pass

        rt = asyncio.create_task(reader())
        await asyncio.sleep(0.8)
        print(f"connected; server said txAudioAvailable={rx['txAudioAvailable']} (text msgs {rx['text']})", flush=True)
        en = {"cmd": "enableMic", "value": True}
        if args.device is not None:
            en["device"] = args.device
        await ws.send(json.dumps(en))
        await asyncio.sleep(0.3)

        gen = frames(args.seconds, args.tone, args.amp)
        period = 128 / 48000.0
        t0 = time.perf_counter()
        k = 0
        sent = 0
        late_max = 0.0
        batch = []
        for f in gen:
            batch.append(f)
            k += 1
            if len(batch) < args.burst:
                continue
            target = t0 + k * period
            now = time.perf_counter()
            if target > now:
                await asyncio.sleep(target - now)
            else:
                late_max = max(late_max, now - target)
            for b in batch:
                await ws.send(b)
                sent += 1
            batch = []
            if args.stall_at and abs(k * period - args.stall_at) < period:
                print(f"stalling {args.stall_ms} ms at {k*period:.2f}s", flush=True)
                await asyncio.sleep(args.stall_ms / 1000.0)
                t0 += args.stall_ms / 1000.0
        for b in batch:
            await ws.send(b)
            sent += 1
        el = time.perf_counter() - t0
        print(f"sent {sent} frames ({sent*128/48000:.2f}s of audio) in {el:.2f}s wall, max lateness {late_max*1000:.1f} ms", flush=True)
        await asyncio.sleep(0.5)
        await ws.send(json.dumps({"cmd": "enableMic", "value": False}))
        await asyncio.sleep(0.3)
        print(f"server micWarning: {rx['warn']}", flush=True)
        rt.cancel()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="wss://localhost:8080")
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--tone", type=float, default=1000)
    ap.add_argument("--amp", type=float, default=0.25)
    ap.add_argument("--burst", type=int, default=1)
    ap.add_argument("--stall-at", type=float, default=0)
    ap.add_argument("--stall-ms", type=int, default=300)
    ap.add_argument("--device", default=None, help="mic label to report with enableMic, like the browser does")
    args = ap.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
