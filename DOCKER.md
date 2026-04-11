# Running wfweb in Docker

The `k1fm/wfweb` Docker image runs wfweb in headless mode with a web interface.
Multi-arch images are published for `linux/amd64` and `linux/arm64`.

---

## Quick Start

### LAN-connected radio (easiest)

No USB devices needed — just network access:

```bash
docker run --rm -it \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb \
  --lan 192.168.1.100 --lan-user admin --lan-pass secret
```

Open `https://localhost:8080` in your browser (accept the self-signed certificate).

### USB-connected radio

```bash
docker run --rm -it \
  --device /dev/ttyUSB0 \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb
```

### USB-connected radio with audio

For RX/TX audio through the radio's USB audio codec:

```bash
docker run --rm -it \
  --device /dev/ttyUSB0 \
  --device /dev/snd \
  --group-add audio \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb
```

---

## USB Serial Port

### Auto-detection

The container tries to find an Icom radio automatically by checking USB serial
numbers. If auto-detection fails (common in Docker since udev metadata may be
incomplete), it falls back to `/dev/ttyUSB0`.

### Explicit serial port

Use `--serial-port` to specify the device path directly:

```bash
docker run --rm -it \
  --device /dev/ttyUSB1 \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb --serial-port /dev/ttyUSB1
```

### Device remapping

Docker can remap a host device to a different path inside the container.
This is useful when the radio appears on a non-default port:

```bash
docker run --rm -it \
  --device=/dev/ttyUSB1:/dev/ttyUSB0 \
  --group-add $(stat -c %g /dev/ttyUSB1) \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb
```

The `--group-add` ensures the container process has permission to access the
device regardless of its ownership on the host.

---

## USB Audio

The IC-7300 and similar radios expose a USB Audio CODEC device for RX/TX audio.
To use it from Docker:

```bash
--device /dev/snd --group-add audio
```

The container uses ALSA for direct hardware access (no PulseAudio daemon runs
inside). The audio device is detected automatically — look for
`Web: Selected rig audio device:` in the logs to confirm.

### Audio backends

The container defaults to **RtAudio** (ALSA) for the internal audio system.
You can override this with `--audio-system`:

| ID | Backend    | Notes                                       |
|----|------------|---------------------------------------------|
| 0  | Qt Audio   | Requires PulseAudio — not useful in Docker  |
| 1  | PortAudio  | Alternative, uses ALSA in container         |
| 2  | RtAudio    | Default in Docker, direct ALSA access       |

---

## LAN Connection

For radios connected via Icom's network protocol (IC-7610, IC-9700, IC-705
in Wi-Fi mode, or any radio through a wfweb server):

```bash
docker run --rm -it \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb \
  --lan 192.168.1.100 \
  --lan-user admin \
  --lan-pass secret
```

No `--device` flags are needed for LAN connections. Audio streams over the
network automatically. If your radio uses a non-default CI-V address, add
`--civ <addr>` (hex `0x94` or decimal `148`).

### LAN port overrides

If the remote radio or server uses non-default ports:

```bash
--lan-control 50001   # Control port (default: 50001)
--lan-serial 50002    # CI-V port (default: 50002)
--lan-audio 50003     # Audio port (default: 50003)
```

---

## Rig Server Mode (`--no-web`)

Use `--no-web` to disable the web interface and enable the Icom UDP rig server
instead. This lets other wfweb instances (or compatible clients) connect to the
radio over the network.

The rig server uses **UDP** ports — you must add `/udp` to the Docker port
mappings:

```bash
docker run --rm -it \
  --device /dev/ttyUSB0 \
  --device /dev/snd --group-add audio \
  -p 50001:50001/udp -p 50002:50002/udp -p 50003:50003/udp \
  k1fm/wfweb --serial-port /dev/ttyUSB0 --no-web
```

> **Note:** `-p 50001:50001` (without `/udp`) only maps TCP, which will not work.

---

## Ports

| Port  | Protocol  | Purpose                              |
|-------|-----------|--------------------------------------|
| 8080  | TCP/HTTPS | Web interface and WebSocket          |
| 8081  | TCP/HTTP  | Plain HTTP REST API (scripts, microcontrollers) |
| 50001 | UDP       | Rig server control (--no-web mode)   |
| 50002 | UDP       | Rig server CI-V (--no-web mode)      |
| 50003 | UDP       | Rig server audio (--no-web mode)     |

Both web ports must be published (`-p`) for the web interface to work.
A self-signed TLS certificate is generated automatically on first run.

Override the web server port with `-p` (CLI flag, not Docker's `-p`):

```bash
docker run --rm -it \
  -p 9090:9090 -p 9091:9091 \
  k1fm/wfweb -p 9090
```

The REST port is always web port + 1.

---

## Persistent Configuration

The container stores settings in `/root/.config/wfview/wfweb.conf`. To persist
configuration across container restarts, mount a volume:

```bash
docker run --rm -it \
  -v wfview-config:/root/.config/wfview \
  --device /dev/ttyUSB0 \
  --device /dev/snd --group-add audio \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb
```

You can also supply a pre-made settings file:

```bash
docker run --rm -it \
  -v /path/to/my-wfweb.conf:/config/wfweb.conf:ro \
  --device /dev/ttyUSB0 \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb -s /config/wfweb.conf
```

A settings file is where wfweb persists **your** preferences — serial
port, audio device, web port, LAN address and credentials, per-radio
config, and so on. You don't hand-write it; wfweb creates it on first
run with sensible defaults and then saves your changes back to it from
the web UI. Mounting one into the container is how you give a Docker
deployment a fixed, predictable config location instead of relying on
the default per-user path inside the container (which vanishes when the
container exits).

If you're running **multiple wfweb containers in parallel** (one per
radio), give each its own mounted settings file so they don't overwrite
each other's state. For **named profiles** you want to switch between
(`home.conf`, `contest.conf`, ...), just mount a different file.

If all you need is to talk to a rig on a non-default CI-V address or a
different manufacturer, you don't need `-s` at all — use `--civ <addr>`
and `--manufacturer <id>` directly.

> **Note:** `-s` does **not** take `.rig` files. Those are CI-V command
> dictionaries for specific radio models that wfweb already loads
> automatically from its install's `rigs/` directory based on the radio
> it detects on the bus.

The TLS certificate is stored in `/root/.local/share/wfview/wfweb/`. Mount that
path too if you want to persist or supply your own certificate.

---

## CLI Reference

All flags are passed after the image name:

```
wfweb [options]

Connection:
  --serial-port <path>    Serial port device (e.g. /dev/ttyUSB0)
  --civ <addr>            CI-V address in decimal (e.g. 148 for IC-7300)
  --manufacturer <id>     Manufacturer (0=Icom, 1=Kenwood, 2=Yaesu)

LAN mode:
  --lan <ip>              Connect via LAN (no USB needed)
  --lan-user <user>       LAN username
  --lan-pass <pass>       LAN password
  --lan-control <port>    Control port (default: 50001)
  --lan-serial <port>     CI-V port (default: 50002)
  --lan-audio <port>      Audio port (default: 50003)

Server:
  -p --port <port>        Web server HTTPS port (default: 8080)
  -S --no-web             Disable web server, enable rig server

Audio:
  --audio-system <id>     Audio backend (0=Qt, 1=PortAudio, 2=RtAudio)

General:
  -s --settings <file>    Path to a wfweb settings file (see "pre-made
                          settings file" above). Does NOT accept .rig
                          files. Most users don't need this flag.
  -l --logfile <file>     Log file path
  -b --background         Run as daemon
  -d --debug              Enable debug logging
  -v --version            Show version
```

### Common CI-V addresses

| Radio        | Decimal | Hex  |
|--------------|---------|------|
| IC-7300      | 148     | 0x94 |
| IC-7300 Mk2  | 130     | 0x82 |
| IC-705       | 164     | 0xA4 |
| IC-7610      | 152     | 0x98 |
| IC-9700      | 162     | 0xA2 |
| IC-785x      | 142     | 0x8E |

---

## Docker Compose

Example `docker-compose.yml` for a USB-connected radio with audio:

```yaml
services:
  wfweb:
    image: k1fm/wfweb
    restart: unless-stopped
    ports:
      - "8080:8080"
      - "8081:8081"
    devices:
      - /dev/ttyUSB0:/dev/ttyUSB0
      - /dev/snd:/dev/snd
    group_add:
      - audio
    volumes:
      - wfview-config:/root/.config/wfview
    command: ["--serial-port", "/dev/ttyUSB0"]

volumes:
  wfview-config:
```

For LAN mode:

```yaml
services:
  wfweb:
    image: k1fm/wfweb
    restart: unless-stopped
    ports:
      - "8080:8080"
      - "8081:8081"
    volumes:
      - wfview-config:/root/.config/wfview
    command:
      - "--lan"
      - "192.168.1.100"
      - "--lan-user"
      - "admin"
      - "--lan-pass"
      - "secret"

volumes:
  wfview-config:
```

---

## Building Locally

```bash
docker build -f docker/Dockerfile -t wfweb .
docker run --rm -it --device /dev/ttyUSB0 -p 8080:8080 -p 8081:8081 wfweb
```

The build uses a multi-stage process: Ubuntu 24.04 for compilation, Ubuntu 25.10
for the runtime image. The final image is roughly 150 MB.

---

## Troubleshooting

### No serial port detected

```
Could not find an Icom serial port. Falling back to OS default.
```

Use `--serial-port /dev/ttyUSBx` to specify the device explicitly. Verify the
device is passed to the container with `--device`.

### No audio devices found

```
<NONE> Audio input device default Not found
```

Ensure you pass `--device /dev/snd` and `--group-add audio`. Check that the
USB audio device is visible on the host with `aplay -l`.

### PulseAudio connection errors

The container uses ALSA directly. If you see PulseAudio warnings, they are
harmless — the system falls back to ALSA automatically.

### Self-signed certificate warning

The container generates a self-signed TLS certificate on first run. Your browser
will show a security warning — this is expected. Accept the certificate to
proceed, or mount your own certificate files.

### Audio works on host but not in container

The container's Qt audio backend uses ALSA directly instead of PulseAudio.
Ensure `/dev/snd` is passed and the `audio` group is added. The logs should
show `Web: Selected rig audio device:` when the USB codec is found.
