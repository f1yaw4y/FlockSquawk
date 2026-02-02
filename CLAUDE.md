# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FlockSquawk is an ESP32 Arduino project for passive RF awareness. It sniffs WiFi management frames and BLE advertisements, matches them against known surveillance device signatures, and outputs alerts via displays, audio, and JSON telemetry over Serial.

## Build System

This is a pure **Arduino IDE** project (no CMake, PlatformIO, or Makefile). Each hardware variant is a self-contained Arduino sketch that opens directly in the IDE.

**Critical constraint:** ESP32 board package must be **version 3.0.7 or older** (newer versions cause IRAM overflow).

### Building with arduino-cli

```bash
# Install ESP32 core
arduino-cli core install esp32:esp32@3.0.7

# Install required libraries
arduino-cli lib install ArduinoJson NimBLE-Arduino M5Unified

# Compile a variant (example: M5StickC Plus2)
arduino-cli compile --fqbn esp32:esp32:m5stick_c_plus2 m5stack/flocksquawk_m5stick/

# Upload
arduino-cli upload --fqbn esp32:esp32:m5stick_c_plus2 --port /dev/ttyUSB0 m5stack/flocksquawk_m5stick/

# Serial monitor (115200 baud for JSON telemetry)
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

Board FQBNs vary per variant — check each variant's README for exact board settings.

## Hardware Variants

Six self-contained variants, each in its own directory with a dedicated README:

| Variant | Path | Display | Audio |
|---|---|---|---|
| M5StickC Plus2 | `m5stack/flocksquawk_m5stick/` | Built-in TFT 135x240 | Buzzer tones |
| M5Stack FIRE | `m5stack/flocksquawk_m5fire/` | Built-in TFT 320x240 | Built-in speaker |
| Mini12864 | `Mini12864/flocksquawk_mini12864/` | ST7567 LCD 128x64 | I2S (MAX98357A) |
| 128x32 OLED | `128x32_OLED/flocksquawk_128x32/` | SSD1306/SH1106 I2C | I2S (MAX98357A) |
| 128x32 Portable | `128x32_OLED/flocksquawk_128x32_portable/` | SSD1306/SH1106 I2C | GPIO buzzer |
| Flipper Zero | `flipper-zero/dev-board-firmware/` | None (UART only) | None |

Variants do **not** share source files — each has its own copy of the core headers in `src/`. Changes to shared logic (EventBus, ThreatAnalyzer, etc.) must be applied to each variant individually.

## Architecture

Data flows through a publish-subscribe pipeline:

```
RadioScannerManager (WiFi promiscuous + BLE scan)
    → EventBus (WiFiFrameEvent / BluetoothDeviceEvent)
        → ThreatAnalyzer (signature matching → ThreatEvent)
            → TelemetryReporter (JSON over Serial)
            → Display (variant-specific UI)
            → SoundEngine (alerts)
```

### Core subsystems (in each variant's `src/`)

- **EventBus.h** — Header-only static publish/subscribe. Event types: `WifiFrameCaptured`, `BluetoothDeviceFound`, `ThreatIdentified`, `SystemReady`.
- **RadioScanner.h** — WiFi promiscuous mode (channels 1-13, 1s hop interval) and BLE scanning via NimBLE (5s interval, 1s duration). Uses FreeRTOS portMUX spinlocks for thread safety between ISR callbacks and main loop.
- **ThreatAnalyzer.h** — Compares observations against `DeviceSignatures.h`. Tracks up to 32 devices with LRU eviction (states: NEW_DETECT → IN_RANGE → DEPARTED). Alert threshold: 65% certainty.
- **DeviceSignatures.h** — Static arrays of known SSIDs, MAC OUI prefixes, BLE device names, and service UUIDs.
- **TelemetryReporter.h** — Serializes detections as JSON via ArduinoJson (StaticJsonDocument<512>).
- **SoundEngine.h** — I2S WAV playback from LittleFS (Mini12864/128x32) or M5.Speaker tone generation (M5Stack variants).

### Pluggable detector system (M5Stick variant only — newest architecture)

The M5Stick variant introduces a function-pointer-based detector registry:

- **DetectorTypes.h** — Defines `DetectorResult` (matched/weight/name) and `WiFiDetectorEntry`/`BLEDetectorEntry` structs.
- **Detectors.h** — Registers detector functions. WiFi: `detectSsidFormat` (weight 75), `detectSsidKeyword` (45), `detectWifiMacOui` (20). BLE: `detectBleName` (55), `detectRavenCustomUuid` (80), `detectRavenStdUuid` (10), `detectBleMacOui` (20).
- Weighted scoring with subsumption (higher-confidence detectors override lower ones) and RSSI-based modifiers.

This detector pattern is the intended direction for all variants.

## Thread Safety Pattern

WiFi promiscuous callbacks and BLE scan callbacks run on different cores/tasks than `loop()`. All variants use the same pattern:

- `portMUX_TYPE` spinlocks guard shared volatile state in ISR callbacks
- `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` for atomic reads/writes
- Main loop copies event data under lock, then processes outside the critical section

## Key Constants

- WiFi channel hop: 1 second
- BLE scan interval: 5 seconds, 1 second duration
- Device tracking slots: 32 (LRU eviction)
- Device departure timeout: 60 seconds
- Heartbeat re-alert interval: 10 seconds
- Alert certainty threshold: 65%
- Serial baud rate: 115200
