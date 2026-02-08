# Build System

FlockSquawk is a pure Arduino project -- each variant is a self-contained sketch that opens directly in the Arduino IDE. For automated and reproducible builds, the project also provides a Makefile and Docker environment.

## Quick Reference

| Tool | Purpose |
|------|---------|
| Arduino IDE | Manual build and upload (open the `.ino` file) |
| `Makefile` | Automated compile, upload, test, LittleFS flashing |
| Docker | Reproducible builds with all dependencies pre-baked |
| `versions.env` | Single source of truth for all dependency versions |

## Dependency Versions

All pinned versions are centralized in `versions.env`:

| Dependency | Variable | Purpose |
|-----------|----------|---------|
| ESP32 board core | `ESP32_CORE_VERSION` | Must be 3.0.7 or older (IRAM overflow) |
| ArduinoJson | `ARDUINOJSON_VERSION` | JSON serialization |
| NimBLE-Arduino | `NIMBLE_VERSION` | BLE scanning |
| M5Unified | `M5UNIFIED_VERSION` | M5Stack hardware abstraction |
| U8g2 | `U8G2_VERSION` | Mini12864 display driver |
| Adafruit GFX + SSD1306 | `ADAFRUIT_GFX_VERSION`, `ADAFRUIT_SSD1306_VERSION` | 128x32 OLED display |
| doctest | `DOCTEST_VERSION` | Host-side unit test framework |

## Makefile

### Variants

Six build targets, one per hardware variant:

| Name | FQBN | Sketch Path |
|------|------|-------------|
| `m5stick` | `esp32:esp32:m5stack_stickc_plus2` | `m5stack/flocksquawk_m5stick` |
| `m5fire` | `esp32:esp32:m5stack_fire` | `m5stack/flocksquawk_m5fire` |
| `mini12864` | `esp32:esp32:esp32` | `Mini12864/flocksquawk_mini12864` |
| `oled` | `esp32:esp32:esp32` | `128x32_OLED/flocksquawk_128x32` |
| `portable` | `esp32:esp32:esp32` | `128x32_OLED/flocksquawk_128x32_portable` |
| `flipper` | `esp32:esp32:esp32s2` | `flipper-zero/dev-board-firmware/flocksquawk-flipper` |

### Common Commands

```bash
# Install all dependencies (ESP32 core + libraries)
make install-deps

# Compile a specific variant
make build-m5stick
make build-oled

# Compile all variants
make all

# Compile + upload (auto-detects serial port)
make flash-m5stick
make flash-oled PORT=/dev/cu.usbserial-0001

# Upload LittleFS audio files (variants with data/ directory)
make upload-data-mini12864 PORT=/dev/ttyUSB0

# Open serial monitor
make monitor

# Run host-side unit tests
make test
make test-verbose

# Clean build artifacts
make clean
```

### Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | auto-detected | Serial port for upload/monitor |
| `BAUD` | `115200` | Serial monitor baud rate |
| `VARIANT` | `m5stick` | Default variant for shorthand targets |
| `CORE_VERSION` | from `versions.env` | ESP32 core version |

### Shorthand Targets

`make build`, `make upload`, `make flash`, and `make monitor` operate on the variant set by `VARIANT` (default: `m5stick`).

## Docker

A Docker image with all toolchains and libraries pre-baked, for zero-install CI or local builds.

### Building the Image

```bash
make docker-build-image
```

This reads versions from `versions.env` and installs the ESP32 core, all Arduino libraries, and warms up the per-board core cache.

### Using Docker

```bash
# Compile all variants
make docker-build-all

# Compile a specific variant
VARIANT=oled make docker-build

# Run tests
make docker-test
make docker-test-verbose

# Interactive shell
make docker-shell
```

### docker-compose

The `docker-compose.yml` provides named services:

```bash
docker compose run build-all          # make all
docker compose run test               # make test
docker compose run shell              # interactive bash
VARIANT=mini12864 docker compose run build-variant  # single variant
```

## arduino-cli (without Makefile)

If you prefer direct arduino-cli commands:

```bash
# Install ESP32 core
arduino-cli core install esp32:esp32@3.0.7

# Install libraries
arduino-cli lib install ArduinoJson NimBLE-Arduino M5Unified U8g2 \
    "Adafruit GFX Library" "Adafruit SSD1306"

# Compile
arduino-cli compile --fqbn esp32:esp32:m5stack_stickc_plus2 \
    --build-property "build.defines=-I$(pwd)/common" \
    m5stack/flocksquawk_m5stick/

# Upload
arduino-cli upload --fqbn esp32:esp32:m5stack_stickc_plus2 \
    --port /dev/ttyUSB0 m5stack/flocksquawk_m5stick/

# Serial monitor
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

Note the `--build-property "build.defines=-I$(pwd)/common"` flag -- this is how shared headers in `common/` are made available to variant sketches. The Makefile handles this automatically.
