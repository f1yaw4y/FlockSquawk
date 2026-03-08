# FlockSquawk

FlockSquawk is a modular, event-driven ESP32 project for **passive RF awareness**. It listens to nearby WiFi and Bluetooth Low Energy activity, analyzes patterns against known signatures, and provides feedback through audio alerts, on-device displays, and structured telemetry.

---

## What the Project Does

At runtime, FlockSquawk:

- Sniffs WiFi management frames using ESP32 promiscuous mode
- Scans nearby BLE advertisements using NimBLE
- Compares observations against signature patterns
  - SSIDs
  - MAC prefixes (OUIs)
  - BLE device names
  - Service UUIDs
- Emits structured JSON events over Serial
- Plays audible alerts via I2S audio
- Displays live status on an attached screen (variant dependent)

---

## Project Goals

- Educational: provide a readable, real-world RF + embedded systems codebase
- Experimental: make it easy to modify detection logic and behaviors
- Modular: allow different hardware front-ends (displays, controls, indicators)

---

## ESP32 Core Version Requirement

Use **esp32 by Espressif Systems** version **3.0.7 or older**. Newer versions fail to compile due to an **IRAM overflow** issue.

---

## Hardware Variants

FlockSquawk supports multiple hardware front-ends. Each variant lives in its own folder and includes its **own dedicated README with wiring and setup instructions**.

| Variant | Path | Display | Audio |
|---------|------|---------|-------|
| [M5StickC Plus2](m5stack/flocksquawk_m5stick/README.md) | `m5stack/flocksquawk_m5stick/` | Built-in TFT | Buzzer tones |
| [M5Stack FIRE](m5stack/flocksquawk_m5fire/README.md) | `m5stack/flocksquawk_m5fire/` | Built-in TFT | Built-in speaker |
| [Mini12864](Mini12864/flocksquawk_mini12864/README.md) | `Mini12864/flocksquawk_mini12864/` | ST7567 LCD 128x64 | I2S (MAX98357A) |
| [128x32 OLED](128x32_OLED/flocksquawk_128x32/README.md) | `128x32_OLED/flocksquawk_128x32/` | SSD1306/SH1106 I2C | I2S (MAX98357A) |
| [128x32 Portable](128x32_OLED/flocksquawk_128x32_portable/README.md) | `128x32_OLED/flocksquawk_128x32_portable/` | SSD1306/SH1106 I2C | GPIO buzzer |
| [Flipper Zero](flipper-zero/README.md) | `flipper-zero/dev-board-firmware/` | None (UART) | None |

Each variant is self-contained and can be opened directly in the Arduino IDE.

---

## Documentation

| Guide | Description |
|-------|-------------|
| [Getting Started](docs/getting-started.md) | Arduino IDE setup, ESP32 board support, shared libraries |
| [Architecture](docs/architecture.md) | Pipeline, event types, detector system, thread safety |
| [Build System](docs/build-system.md) | Makefile, Docker, arduino-cli, versions.env |
| [Configuration](docs/configuration.md) | WiFi/BLE tuning, detection patterns, audio/volume |
| [Telemetry Format](docs/telemetry-format.md) | JSON schema reference, Flipper UART protocol |
| [Testing](docs/testing.md) | Host-side doctest tests, mocks, running tests |
| [Extending](docs/extending.md) | Adding detectors, patterns, subscribers, new variants |
| [Troubleshooting](docs/troubleshooting.md) | Common issues: compilation, upload, no detections |

---

## Repository Structure

```
FlockSquawk/
├── common/                          # Shared headers (EventBus, Detectors, ThreatAnalyzer, ...)
├── docs/                            # Project-wide documentation
├── m5stack/
│   ├── flocksquawk_m5stick/         # M5StickC Plus2 variant
│   └── flocksquawk_m5fire/          # M5Stack FIRE variant
├── Mini12864/
│   └── flocksquawk_mini12864/       # Mini12864 LCD variant
├── 128x32_OLED/
│   ├── flocksquawk_128x32/          # 128x32 OLED + I2S audio variant
│   └── flocksquawk_128x32_portable/ # 128x32 OLED + buzzer variant
├── flipper-zero/
│   ├── dev-board-firmware/          # Flipper Zero ESP32-S2 firmware
│   └── flock_scanner.fap           # Flipper Zero companion app
├── test/                            # Host-side unit tests
├── Makefile                         # Build automation
├── Dockerfile                       # Reproducible build environment
├── versions.env                     # Pinned dependency versions
└── README.md                        # This file
```

If you are trying to build the project, start by entering one of the variant folders and follow that README, or see [Getting Started](docs/getting-started.md).

---

## Core Architecture

All variants share the same core subsystems:

- **RadioScannerManager** -- Handles WiFi promiscuous mode and BLE scanning
- **ThreatAnalyzer** -- Compares observed data against signature patterns
- **EventBus** -- Lightweight publish/subscribe system connecting components
- **SoundEngine** -- I2S-based WAV playback or buzzer tones (variant-specific)
- **TelemetryReporter** -- Emits structured JSON output over Serial

Because of this structure, new interfaces can be added cleanly: displays, LEDs, network reporting, logging, etc. See [Architecture](docs/architecture.md) for details.

---

## License

[GNU GENERAL PUBLIC LICENSE](https://github.com/f1yaw4y/FlockSquawk/blob/main/LICENSE)

---

## Acknowledgments

- Inspired by [flock-you](https://github.com/colonelpanichacks/flock-you)
- ESP32 community for excellent hardware support
- NimBLE-Arduino for efficient BLE scanning
- ArduinoJson for flexible JSON handling
