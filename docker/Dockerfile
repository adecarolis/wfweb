# Multi-stage build for wfweb
# Supports linux/amd64 and linux/arm64

FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git \
    emscripten \
    qt5-qmake qtbase5-dev libqt5serialport5-dev \
    qtmultimedia5-dev libqt5websockets5-dev \
    libqt5gamepad5-dev libqt5printsupport5 \
    libopus-dev libeigen3-dev \
    portaudio19-dev librtaudio-dev \
    libhidapi-dev libudev-dev libpulse-dev \
    libqcustomplot-dev \
    libcodec2-dev \
    cmake autoconf automake libtool curl \
    openssl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN bash resources/build-wspr-decoder-wasm.sh

# Build RADE (radae_nopy submodule) — clean any stale host cache first
RUN if [ -f radae_nopy/src/rade_api.h ]; then \
      cd radae_nopy && rm -rf build && mkdir build && cd build \
      && cmake -DCMAKE_BUILD_TYPE=Release .. \
      && make -j$(nproc); \
    fi

RUN qmake wfweb.pro && make -j$(nproc)
RUN make install INSTALL_ROOT=/install

# Copy librade.so to install tree for the runtime stage
RUN if [ -f radae_nopy/build/src/librade.so ]; then \
      mkdir -p /install/usr/lib \
      && cp radae_nopy/build/src/librade.so* /install/usr/lib/; \
    fi

# Runtime stage
FROM ubuntu:25.10

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    openssl \
    libqt5core5t64 libqt5gui5t64 libqt5widgets5t64 \
    libqt5network5t64 libqt5serialport5 libqt5multimedia5 \
    libqt5websockets5 libqt5gamepad5 libqt5printsupport5t64 \
    libqt5multimedia5-plugins libasound2-data \
    libopus0 libportaudio2 libpulse0 libhidapi-libusb0 \
    libudev1 udev libqcustomplot2.1 libcodec2-1.2 \
    && rm -rf /var/lib/apt/lists/*

# The binary links against librtaudio v6 (24.04) but 25.04 ships v7 with an
# incompatible ABI. Copy the v6 shared library from the builder stage.
COPY --from=builder /usr/lib/*/librtaudio.so.6* /usr/lib/
RUN ldconfig
COPY --from=builder /install/usr/local /usr/local
COPY --from=builder /install/usr/lib/librade* /usr/lib/
COPY --from=builder /src/LICENSE /src/THIRD_PARTY_LICENSES /usr/share/doc/wfweb/
RUN ldconfig

ENV QT_QPA_PLATFORM=offscreen

# Remove the PulseAudio Qt plugin so Qt Multimedia uses ALSA directly.
# No PulseAudio daemon runs inside the container; leaving the plugin causes
# noisy "Connection refused" warnings on every audio enumeration.
RUN rm -f /usr/lib/*/qt5/plugins/audio/libqtmedia_pulse.so

EXPOSE 8080 8081

ENTRYPOINT ["wfweb"]
CMD []
