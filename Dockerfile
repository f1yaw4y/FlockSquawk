# FlockSquawk Docker Build Environment
# All toolchains and libraries pre-baked for zero-install builds.
#
# Build:  docker build -t flocksquawk-build:latest .
# Run:    docker run --rm -v .:/workspace flocksquawk-build:latest make all

FROM debian:bookworm-slim

# ── 1. System packages ──────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
        clang \
        make \
        python3 \
        python3-serial \
        curl \
        ca-certificates \
        git \
    && rm -rf /var/lib/apt/lists/*

# ── 2. arduino-cli (pinned latest stable) ───────────────────────────
RUN mkdir -p /tmp/acli \
    && cd /tmp/acli \
    && curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh -s -- \
    && mv bin/arduino-cli /usr/local/bin/ \
    && rm -rf /tmp/acli

# ── 3. ESP32 board manager URL ──────────────────────────────────────
RUN arduino-cli config init \
    && arduino-cli config add board_manager.additional_urls \
       https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# ── 4. ESP32 core v3.0.7 (~1.5 GB — includes Xtensa + RISC-V) ─────
RUN arduino-cli core update-index \
    && arduino-cli core install esp32:esp32@3.0.7

# ── 5. Arduino libraries (version-pinned) ───────────────────────────
RUN arduino-cli lib install \
        ArduinoJson@7.3.0 \
        "NimBLE-Arduino@2.2.1" \
        M5Unified@0.2.2 \
        U8g2@2.35.30 \
        "Adafruit GFX Library@1.12.4" \
        "Adafruit SSD1306@2.5.13"

# ── 6. doctest.h (pre-fetched for host-side tests) ──────────────────
RUN mkdir -p /opt/flocksquawk-deps \
    && curl -sL -o /opt/flocksquawk-deps/doctest.h \
       https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h

# ── 7. Core cache warm-up ───────────────────────────────────────────
# Dummy-compile a minimal sketch against each distinct FQBN so the
# per-board core cache is pre-generated inside the image.  This avoids
# a ~60 s first-build penalty at runtime.
RUN mkdir -p /tmp/warmup/warmup \
    && echo 'void setup(){} void loop(){}' > /tmp/warmup/warmup/warmup.ino \
    && for fqbn in \
         esp32:esp32:m5stack_stickc_plus2 \
         esp32:esp32:m5stack_fire \
         esp32:esp32:esp32 \
         esp32:esp32:esp32s2 ; do \
       echo "Warming core cache for ${fqbn} ..." \
       && arduino-cli compile --fqbn "${fqbn}" /tmp/warmup/warmup || true ; \
    done \
    && rm -rf /tmp/warmup

# ── 8. Workspace ────────────────────────────────────────────────────
WORKDIR /workspace

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["entrypoint.sh"]
CMD ["make", "help"]
