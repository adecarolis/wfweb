# wfweb

**Control your Icom radio from any browser — phone, tablet, or desktop.**

wfweb turns your transceiver into a web-accessible station. Waterfall, audio, CW decoding, FT8/FT4, FreeDV digital voice, and RADE — all in the browser, no client software required.

![FT8 digital mode panel](ft8.png)

![SSB mode](ssb.png)

![CW mode with decoder](cw.png)

---

## IC-7300 Mk2 with Ethernet? Just run Docker

The IC-7300 Mk2 has a built-in Ethernet port. No USB cable, no drivers, no build — just pull and run:

```bash
docker run --rm -it \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb --lan 192.168.1.100 --civ 130 --lan-user admin --lan-pass secret
```

Replace the IP, username, and password with your radio's settings. Open `https://<host>:8080` in your browser, accept the self-signed certificate warning, and you're on the air.

The Docker image is multi-arch (`linux/amd64` and `linux/arm64`) — it runs on x86 servers, Raspberry Pi, and everything in between.

---

## USB connection? Grab a build

For radios connected via USB, download a pre-built binary from [GitHub Releases](../../releases) for your platform:

| Platform | Package | Distro |
|---|---|---|
| **Linux x86_64** | `.deb` (ubuntu2404) | Ubuntu 24.04 Noble |
| **Linux x86_64** | `.deb` (debian12) | Debian 12 Bookworm |
| **Linux x86_64** | `.deb` (debian13) | Debian 13 Trixie |
| **Linux ARM64 / Raspberry Pi** | `.deb` (ubuntu2404) | Ubuntu 24.04 Noble |
| **Linux ARM64 / Raspberry Pi** | `.deb` (debian12) | Raspberry Pi OS Bookworm / Debian 12 |
| **Linux ARM64 / Raspberry Pi** | `.deb` (debian13) | Raspberry Pi OS Trixie / Debian 13 |
| **macOS** | zip | Apple Silicon |
| **Windows** | zip | x86_64 |

> **Which `.deb` do I need?** The tag in the filename tells you:
> **ubuntu2404** for Ubuntu 24.04,
> **debian12** for Debian 12 Bookworm and Raspberry Pi OS Bookworm,
> **debian13** for Debian 13 Trixie and Raspberry Pi OS Trixie.
> Each is built natively on its target distro so the library dependencies match.

Plug in your radio and run:

```bash
# IC-7300 — zero config, auto-detected
./wfweb

# IC-7300 Mk2
./wfweb --civ 130

# IC-7610
./wfweb --civ 152

# IC-705
./wfweb --civ 164
```

Open `https://<host>:8080` in your browser. Accept the self-signed certificate warning. That's it.

> For LAN connections from a native build, add `--lan <ip>` and credentials — see [Command-line options](#command-line-options) below.

---

## What wfweb adds over wfview

wfweb is a fork of [wfview](https://gitlab.com/eliggett/wfview), the outstanding open-source front-end for Icom, Kenwood, and Yaesu transceivers by Elliott H. Liggett W6EL, Phil E. Taylor M0VSE, and contributors.

Everything wfview does, wfweb does too — plus a built-in web interface:

| Feature | wfview | wfweb |
|---|:---:|:---:|
| Desktop GUI (Qt) | ✓ | ✓ |
| Full radio control (CI-V, LAN) | ✓ | ✓ |
| Waterfall display | ✓ | ✓ |
| Audio over LAN | ✓ | ✓ |
| Built-in HTTP/WebSocket server | — | ✓ |
| Browser-based remote control | — | ✓ |
| Browser RX audio streaming | — | ✓ |
| Browser TX audio (mic to rig) | — | ✓ |
| CW decoder (ggmorse / Goertzel) | — | ✓ |
| FT8/FT4 DIGI panel (full QSO) | — | ✓ |
| FreeDV digital voice (700D/700E) | — | ✓ |
| RADE (Radio Autoencoder) | — | ✓ |
| Mobile-responsive UI | — | ✓ |
| Headless / no-display operation | — | ✓ |

---

## FreeDV and RADE digital voice

wfweb is the first web-based transceiver interface with built-in [FreeDV](https://freedv.org/) support. Operate FreeDV digital voice modes directly from your browser — no additional software needed on the client side.

All FreeDV processing happens **server-side**: the server encodes and decodes modem tones in real time, so browser clients send and receive normal speech audio while the radio transmits and receives FreeDV signals over SSB.

### Supported modes

| Mode | Type | Description |
|------|------|-------------|
| **700D** | Classic FreeDV | Proven HF digital voice, OFDM modem, works well on noisy channels |
| **700E** | Classic FreeDV | Improved 700D variant with better performance on fast-fading channels |
| **RADE** | Radio Autoencoder | Next-generation ML-based codec using neural network inference for high-quality low-bitrate voice over HF |

### How it works

```
RX:  Radio (modem tones) → FreeDV/RADE decode → speech audio → browser
TX:  Browser (speech) → FreeDV/RADE encode → modem tones → radio (SSB)
```

Select the FreeDV mode from the web UI, key up, and talk — wfweb handles the rest.

### Platform support

| Platform | FreeDV 700D/700E | RADE |
|----------|:---:|:---:|
| Linux x86_64 | ✓ | ✓ |
| Linux ARM64 / Raspberry Pi | ✓ | ✓ |
| macOS (Apple Silicon) | ✓ | ✓ |
| Docker | ✓ | ✓ |
| Windows x86_64 | ✓ | ✓ |

### Performance

RADE uses real-time neural network inference. Expect roughly **40% CPU usage** on a mid-range laptop (e.g. Intel i5-10310U @ 1.70 GHz). The classic FreeDV modes (700D/700E) are much lighter and run comfortably on a Raspberry Pi.

---

## Command-line options

All settings can be passed as CLI flags. Run `wfweb --help` for the full list.

| Flag | Description | Default |
|---|---|---|
| `-s --settings <file>` | Settings .ini file | `~/.config/wfview/wfweb.conf` |
| `-p --port <port>` | Web server HTTPS port | `8080` |
| `-S --no-web` | Disable web server, enable rig server | web server enabled |
| `--lan <ip>` | Connect via LAN/UDP (enables LAN mode) | USB serial |
| `--lan-control <port>` | LAN control port | `50001` |
| `--lan-serial <port>` | LAN serial/CI-V port | `50002` |
| `--lan-audio <port>` | LAN audio port | `50003` |
| `--lan-user <user>` | LAN username | (empty) |
| `--lan-pass <pass>` | LAN password | (empty) |
| `--civ <addr>` | CI-V address (decimal) | auto-detect |
| `--manufacturer <id>` | 0=Icom, 1=Kenwood, 2=Yaesu | `0` (Icom) |
| `-l --logfile <file>` | Log to file | `/tmp/wfweb-*.log` |
| `-b --background` | Run as daemon (Linux/macOS) | foreground |
| `-c --clearconfig CONFIRM` | Reset all saved settings and exit | — |
| `-d --debug` | Enable debug logging | off |

---

## CI-V address table

Default CI-V addresses for supported radios (decimal values for `--civ`):

| Radio | CI-V (hex) | CI-V (decimal) |
|---|:---:|:---:|
| IC-7300 | 0x94 | 148 (auto-detected) |
| IC-7300 Mk2 | 0x82 | 130 |
| IC-705 | 0xA4 | 164 |
| IC-7610 | 0x98 | 152 |
| IC-9700 | 0xA2 | 162 |
| IC-7100 | 0x88 | 136 |
| IC-7410 | 0x80 | 128 |

---

## Docker details

### USB-connected radios

```bash
docker run --rm -it \
  --device /dev/ttyUSB0 \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb
```

Pass CLI flags after the image name (e.g. `--civ 130`). For a different serial port:

```bash
docker run --rm -it \
  --device /dev/ttyUSB1 \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb --serial-port /dev/ttyUSB1
```

### USB audio (TX/RX through the radio's USB audio codec)

```bash
docker run --rm -it \
  --device /dev/ttyUSB0 \
  --device /dev/snd \
  --group-add audio \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb
```

### IC-7300 (original) via USB on Linux

The original IC-7300 connects via USB, which provides both a serial port and a USB audio codec. Pass the serial device, the sound subsystem, and the `--audio-device` flag to route TX/RX audio through the radio:

```bash
docker run --rm -it \
  --device /dev/ttyUSB1 \
  --device /dev/snd --group-add audio \
  -p 8080:8080 -p 8081:8081 \
  k1fm/wfweb --serial-port /dev/ttyUSB1 --audio-device 'USB Audio CODEC'
```

Adjust `/dev/ttyUSB1` to match your system (`ls /dev/ttyUSB*` to find it).

### Building the image locally

```bash
docker build -f docker/Dockerfile -t wfweb .
docker run --rm -it --device /dev/ttyUSB0 -p 8080:8080 -p 8081:8081 wfweb
```

---

## Configuration file

For persistent configuration, create an `.ini` file and pass it with `-s`:

```ini
[Program]
hasRunSetup=true

[Radio]
Manufacturer=0
RigCIVuInt=130
SerialPortRadio=auto
SerialPortBaud=115200
```

For LAN connections:

```ini
[Program]
hasRunSetup=true

[Radio]
Manufacturer=0
RigCIVuInt=130

[LAN]
EnableLAN=true
IPAddress=192.168.1.100
ControlLANPort=50001
SerialLANPort=50002
AudioLANPort=50003
Username=admin
Password=
```

### Key configuration parameters

| Key | Section | Description | Example |
|---|---|---|---|
| `hasRunSetup` | `[Program]` | Skip first-time setup dialog | `true` |
| `Manufacturer` | `[Radio]` | 0=Icom, 1=Kenwood, 2=Yaesu | `0` |
| `RigCIVuInt` | `[Radio]` | CI-V address (decimal) | `148` |
| `SerialPortRadio` | `[Radio]` | Serial port, or `auto` | `/dev/ttyUSB0` |
| `SerialPortBaud` | `[Radio]` | Baud rate | `115200` |
| `AudioOutput` | `[LAN]` | Local server audio output device (optional) | `hw:CARD=CODEC,DEV=0` |
| `AudioInput` | `[LAN]` | Local server audio input device (optional) | `hw:CARD=CODEC,DEV=0` |

> Audio streams directly between the radio and the browser — no server-side audio configuration is needed for web operation.

---

## Building from source

See **[BUILDING.md](BUILDING.md)** for platform-specific prerequisites and build instructions (Linux, macOS, Windows).

---

## Upstream relationship

wfweb tracks upstream wfview `master`. The delta is kept small — changes are limited to the web server, web frontend, headless build config, and this README. See the [wfview project](https://gitlab.com/eliggett/wfview) for the core radio engine.

---

## Credits

Full credit for the radio control engine, audio subsystem, waterfall, and everything else that makes this work goes to the wfview authors and contributors:

- Elliott H. Liggett, W6EL
- Phil E. Taylor, M0VSE
- Roeland Jansen, PA3MET
- Jim Nijkamp, PA8E
- And the entire wfview community

Please support the original project at **https://wfview.org** and **https://www.patreon.com/wfview**.

The FT8/FT4 DIGI panel is powered by [ft8ts](https://github.com/e04/ft8ts) by e04.
The CW decoder uses [ggmorse](https://github.com/ggerganov/ggmorse) by Georgi Gerganov.
FreeDV digital voice uses [codec2](https://github.com/drowe67/codec2) by David Rowe VK5DGR and contributors, and [radae_nopy](https://github.com/peterbmarks/radae_nopy) by Peter Marks VK5APM (a standalone C implementation of the RADE Radio Autoencoder).

---

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).

All third-party components retain their original licenses — see [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES) for the full text of each.

---

## Disclaimer

This software is provided "as is", without warranty of any kind. It is intended for use by licensed amateur radio operators in compliance with their country's regulations. The authors accept no liability for unlicensed or non-compliant use.
