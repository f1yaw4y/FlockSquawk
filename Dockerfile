# FlockSquawk Docker Build Environment
# All toolchains and libraries pre-baked for zero-install builds.
#
# Build:  make docker-build-image            (reads versions.env)
#    or:  docker build -t flocksquawk-build . (uses ARG defaults below)
# Run:    docker run --rm -v .:/workspace flocksquawk-build:latest make all

ARG BASE_IMAGE=debian:trixie-slim
FROM ${BASE_IMAGE}

# ── Dependency versions ──────────────────────────────────────────────
# Defaults here mirror versions.env.  When building via the Makefile,
# versions.env is the source of truth and overrides these via --build-arg.
ARG ARDUINO_CLI_VERSION=1.4.1
ARG ESP32_CORE_VERSION=3.0.7
ARG ARDUINOJSON_VERSION=7.4.2
ARG NIMBLE_VERSION=2.3.7
ARG M5UNIFIED_VERSION=0.2.11
ARG U8G2_VERSION=2.35.30
ARG ADAFRUIT_GFX_VERSION=1.12.4
ARG ADAFRUIT_SSD1306_VERSION=2.5.16
ARG DOCTEST_VERSION=2.4.12

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

# ── 2. arduino-cli ──────────────────────────────────────────────────
RUN mkdir -p /tmp/acli \
    && cd /tmp/acli \
    && curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh -s -- ${ARDUINO_CLI_VERSION} \
    && mv bin/arduino-cli /usr/local/bin/ \
    && rm -rf /tmp/acli

# ── 3. ESP32 board manager URL ──────────────────────────────────────
RUN arduino-cli config init \
    && arduino-cli config add board_manager.additional_urls \
       https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# ── 4. ESP32 core (~1.5 GB — includes Xtensa + RISC-V) ─────────────
# Pinned to 3.0.7: newer versions cause IRAM overflow on these targets.
RUN arduino-cli core update-index \
    && arduino-cli core install esp32:esp32@${ESP32_CORE_VERSION}

# ── 5. Arduino libraries ────────────────────────────────────────────
RUN arduino-cli lib install \
        ArduinoJson@${ARDUINOJSON_VERSION} \
        "NimBLE-Arduino@${NIMBLE_VERSION}" \
        M5Unified@${M5UNIFIED_VERSION} \
        U8g2@${U8G2_VERSION} \
        "Adafruit GFX Library@${ADAFRUIT_GFX_VERSION}" \
        "Adafruit SSD1306@${ADAFRUIT_SSD1306_VERSION}"

# ── 6. doctest.h (pre-fetched for host-side tests) ──────────────────
RUN mkdir -p /opt/flocksquawk-deps \
    && curl -sL -o /opt/flocksquawk-deps/doctest.h \
       https://raw.githubusercontent.com/doctest/doctest/v${DOCTEST_VERSION}/doctest/doctest.h

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
