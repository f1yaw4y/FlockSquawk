# CLAUDE.md

FlockSquawk is an ESP32 Arduino project for passive RF awareness. It sniffs WiFi management frames and BLE advertisements, matches them against known surveillance device signatures, and outputs alerts via displays, audio, and JSON telemetry.

## Quick Reference

| Item | Value |
|------|-------|
| ESP32 core | **3.0.7 or older** (newer = IRAM overflow) |
| Serial baud | 115200 |
| Build tools | Arduino IDE, Makefile (`make help`), Docker |
| Dependency versions | `versions.env` (single source of truth) |
| Tests | `make test` (host-side doctest, no hardware needed) |

## Pipeline

```
RadioScannerManager (WiFi promiscuous + BLE scan)
    -> EventBus (WiFiFrameEvent / BluetoothDeviceEvent)
        -> ThreatAnalyzer (signature matching -> ThreatEvent)
            -> TelemetryReporter (JSON over Serial)
            -> Display (variant-specific UI)
            -> SoundEngine (alerts)
```

## Variants

| Variant | FQBN | Sketch Path | Display | Audio |
|---------|------|-------------|---------|-------|
| M5StickC Plus2 | `esp32:esp32:m5stack_stickc_plus2` | `m5stack/flocksquawk_m5stick/` | Built-in TFT | Buzzer tones |
| M5Stack FIRE | `esp32:esp32:m5stack_fire` | `m5stack/flocksquawk_m5fire/` | Built-in TFT | Built-in speaker |
| Mini12864 | `esp32:esp32:esp32` | `Mini12864/flocksquawk_mini12864/` | ST7567 LCD 128x64 | I2S (MAX98357A) |
| 128x32 OLED | `esp32:esp32:esp32` | `128x32_OLED/flocksquawk_128x32/` | SSD1306/SH1106 I2C | I2S (MAX98357A) |
| 128x32 Portable | `esp32:esp32:esp32` | `128x32_OLED/flocksquawk_128x32_portable/` | SSD1306/SH1106 I2C | GPIO buzzer |
| Flipper Zero | `esp32:esp32:esp32s2` | `flipper-zero/dev-board-firmware/flocksquawk-flipper/` | None (UART) | None |

Each variant has its own README with wiring, board settings, and usage instructions.

## Source Layout

```
common/                  # Shared headers included via -I common
  EventBus.h             # Pub/sub event bus
  DetectorTypes.h        # DetectorResult, flags, TrackedDevice, constants
  Detectors.h            # All detector functions (WiFi + BLE)
  DeviceSignatures.h     # MAC OUI prefix table
  ThreatAnalyzer.h       # Scoring pipeline + DeviceTracker
  TelemetryReporter.h    # JSON serialization

<variant>/src/           # Variant-specific hardware code
  RadioScanner.h         # WiFi promiscuous + BLE init (per-variant)
  SoundEngine.h          # Audio output (per-variant)
  DisplayEngine.h / ...  # Display driver (some variants)

test/                    # Host-side unit tests (doctest)
```

Shared headers live in `common/` and are included at compile time via `-I common` (set automatically by the Makefile). Each variant also has its own `src/` for hardware-specific code. Some variants still have local copies of shared headers in `src/` from before the migration.

## Thread Safety

- `portMUX_TYPE` spinlocks guard shared volatile state in ISR callbacks
- `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` for atomic reads/writes
- Main loop copies event data under lock, then processes outside the critical section

## When Modifying Code

| Task | Key files | Docs |
|------|-----------|------|
| Add/change detection patterns | `common/Detectors.h`, `common/DeviceSignatures.h` | [docs/extending.md](docs/extending.md) |
| Write a new detector | `common/Detectors.h`, `common/DetectorTypes.h`, `common/ThreatAnalyzer.h` | [docs/extending.md](docs/extending.md) |
| Understand scoring/alerts | `common/ThreatAnalyzer.h`, `common/DetectorTypes.h` | [docs/architecture.md](docs/architecture.md) |
| Change telemetry output | `common/TelemetryReporter.h` | [docs/telemetry-format.md](docs/telemetry-format.md) |
| Tune WiFi/BLE parameters | Variant's `src/RadioScanner.h` | [docs/configuration.md](docs/configuration.md) |
| Build/compile/upload | `Makefile`, `versions.env` | [docs/build-system.md](docs/build-system.md) |
| Run or add tests | `test/`, `Makefile` | [docs/testing.md](docs/testing.md) |
| Add a new variant | New directory + Makefile entry | [docs/extending.md](docs/extending.md) |
