# Virtual Rig Simulator

A standalone Qt binary that presents itself as **N fake Icom IC-7300 transceivers** over the LAN UDP protocol, routing audio between them through a software mixer. One machine, no RF, no extra hardware — a closed-loop radio-to-radio test bench for FreeDV / RADE codec work, mesh experiments, or any multi-station scenario you'd otherwise need two rigs and two operators for.

Each virtual rig listens on its own `control / civ / audio` UDP triplet. A `wfweb` instance connects to it exactly as if it were a real rig over LAN. When rig A keys PTT, its mic audio is bandlimited, mixed, attenuated, and delivered to every other rig whose frequency and mode accept it.

## Building

Requires the same Qt 5 toolchain as `wfweb` proper. From this directory:

```sh
qmake && make -j$(nproc)
```

Produces `./virtualrig`. The binary reuses `icomserver.cpp`, `rigserver.cpp`, and friends directly from the parent repo — no copies.

## Quick start

Easiest path is the orchestrator script one level up:

```sh
./scripts/testrig.sh up 2        # 2 virtual rigs + 2 wfweb clients
./scripts/testrig.sh logs        # tail virtualrig log
./scripts/testrig.sh status
./scripts/testrig.sh down
```

`up` spawns `virtualrig`, then one `wfweb` per virtual rig, each on its own web-UI port and each LAN-connected to its assigned rig. The script prints URLs when ready:

```
Rig #0  "virtual-IC7300-A"   https://127.0.0.1:9080
Rig #1  "virtual-IC7300-B"   https://127.0.0.1:9090

Bench control panel:          http://127.0.0.1:5900
```

Extra args after the count are forwarded to `virtualrig`:

```sh
./scripts/testrig.sh up 3 --noise 200 --atten 0.05 --broadcast
```

## Running by hand

```sh
./virtualrig --rigs 2
```

| Flag | Default | What |
|---|---|---|
| `--rigs N` | 2 | Number of virtual rigs (1..16). Each gets a unique `A..P` label and a stable GUID derived from its name. |
| `--base-port PORT` | 50001 | Rig 0's control port. Rig `i` uses `PORT + i*10` (control), `+1` (CI-V), `+2` (audio). |
| `--atten GAIN` | 0.1 | Initial linear gain on every inter-rig link (≈ -20 dB). Editable live in the control panel. |
| `--noise RMS` | 0 | Per-rig noise floor (Int16 RMS, 0..1000). 50 ≈ quiet band; 500 ≈ noisy. |
| `--broadcast` | off | Disable frequency/mode gating — every rig hears every other rig regardless of tuning. |
| `--control-port PORT` | 5900 | HTTP control panel port. 0 disables. |

## Control panel

`http://127.0.0.1:5900` (default).

- **Rigs table** — live freq, band, mode, PTT indicator, editable noise floor (RMS, 0..1000).
- **Path attenuation (symmetric)** — per-band dB value between each rig pair. Editing `A ↔ B` on 20 m only affects traffic when one of those rigs is transmitting on 20 m; other bands are independent.
- **Build stamp** — compile-time `__DATE__/__TIME__` in the footer; makes it obvious which binary is answering when restarts and browser caches disagree.

Values are polled every 500 ms; writes are POSTed immediately and the poll loop reconciles. The UI preserves your current keystroke — you can type full values without the poll clobbering the input.

## What the mixer models

- **Channel routing** (on by default). Rig A's TX reaches rig B only if both are in the same mode *and* their frequencies are within B's receiver passband. Passband widths are mode-approximate (SSB ≈ 3 kHz, FM ≈ 10 kHz, CW/RTTY 500 Hz, AM/DV 5 kHz). `--broadcast` disables this gate for simple tone tests.
- **TX bandwidth filter**. Each rig low-passes its outgoing audio with a 2nd-order Butterworth biquad tuned to the mode's nominal bandwidth (SSB 3.5 kHz, FM 12 kHz, CW/RTTY 500 Hz, AM/DV 5 kHz), so a USB rig can't dump 10 kHz of mic audio onto the bus.
- **Per-link attenuation** (per-band). Default 0.1 linear (-20 dB). Symmetric — `A → B` and `B → A` share one value.
- **Per-rig noise floor**. Additive white Gaussian noise at the configured RMS, mixed into every chunk delivered to that rig's client. Preserves a live noise floor even when nobody's transmitting.
- **Receiver cadence**. Each rig emits a strict 20 ms chunk of PCM every 20 ms — real mixer audio when there is any, silence otherwise. This keeps the wfweb client's jitter buffer in one mode and prevents TX audio (e.g. RADE bursts) from interleaving with gaps.

## Configuration of the wfweb clients

`testrig.sh` generates one settings file per wfweb instance in `.testrig/wfweb_i.ini` and passes it with `-s`. Each instance binds its web UI to `9080 + i*10`, LAN-connects to rig `i`'s ports on 127.0.0.1, and authenticates with `wfweb / wfweb`. Delete `.testrig/` to reset everything.

## Debugging

- **virtualrig log**: `.testrig/virtualrig.log` — CI-V traffic, PTT transitions, rig state, control-panel listen status.
- **wfweb logs**: `.testrig/wfweb_i.log` — per-client TX/RX audio, FreeDV/RADE status.
- If `./scripts/testrig.sh up` reports `virtualrig exited immediately`, tail the log; most likely a port conflict from a stale run (`./scripts/testrig.sh down` first, then retry).
- The control panel's build-stamp footer is the fastest way to tell if a browser tab is showing a stale UI from a cached response.

## Architecture brief

```
  wfweb client  ──LAN UDP──▶  icomServer (in own QThread)
                                    │ haveDataFromServer / haveAudioData
                                    ▼
                              virtualRig  ── per-mode TX biquad LPF
                                    │
                                    ▼
                              channelMixer  ── per-band attenuation
                                    │           channel routing gate
                                    ▼
                              rxAudioForRig → virtualRig → mixing buffer
                                                          (20 ms strict cadence)
                                    │
                                    ▼
                              icomServer::receiveAudioData  ──LAN UDP──▶  wfweb client
```

- `icomServer` is reused unmodified from `wfweb` — the only upstream change this branch makes is a null-guard on `radio->rig` plus two small LAN-mode fixes. See the commit history for details.
- `civEmulator` answers the CI-V commands wfweb's UI actually sends: freq/mode read/write, PTT, gain reads, S-meter, model ID. Anything unrecognized gets ACK'd.
- `channelMixer` holds the state for attenuation / noise / routing. All mutations are mutex-guarded so the HTTP control server can poke values live without racing the audio path.
