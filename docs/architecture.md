# Architecture

## Pipeline

Data flows through a publish-subscribe pipeline:

```
RadioScannerManager (WiFi promiscuous + BLE scan)
    -> EventBus (WiFiFrameEvent / BluetoothDeviceEvent)
        -> ThreatAnalyzer (signature matching -> ThreatEvent)
            -> TelemetryReporter (JSON over Serial)
            -> Display (variant-specific UI)
            -> SoundEngine (alerts)
```

## Event Types

Defined in `common/EventBus.h`:

| Event | Published when | Consumed by |
|-------|---------------|-------------|
| `WifiFrameCaptured` | WiFi promiscuous callback fires | ThreatAnalyzer |
| `BluetoothDeviceFound` | BLE scan finds an advertiser | ThreatAnalyzer |
| `ThreatIdentified` | Signature match above threshold | TelemetryReporter, Display, SoundEngine |
| `SystemReady` | All subsystems initialized | Display, SoundEngine |
| `AudioPlaybackRequested` | Alert or status sound needed | SoundEngine |

## Core Subsystems

### EventBus (`common/EventBus.h`)

Header-only static publish/subscribe bus. Single handler per event type. Handlers are `std::function` callbacks registered via `subscribe*()` methods and invoked by `publish*()` methods.

### RadioScanner (variant `src/RadioScanner.h`)

WiFi promiscuous mode scanning (channels 1-13) and BLE scanning via NimBLE. Each variant has its own RadioScanner since hardware initialization differs. Uses FreeRTOS `portMUX_TYPE` spinlocks for thread safety between ISR callbacks and the main loop.

### ThreatAnalyzer (`common/ThreatAnalyzer.h`)

Runs detectors against observations and manages device presence tracking.

- **DeviceTracker**: Tracks up to 32 devices with LRU eviction. States: `EMPTY` -> `NEW_DETECT` -> `IN_RANGE` -> `DEPARTED`. Departure timeout: 60 seconds.
- **Detector scoring**: Runs all registered detectors, sums weights, applies subsumption rules and RSSI modifier, clamps to 0-100.
- **Alert logic**: `shouldAlert` is true only on first detection of a device above the 65% certainty threshold. Heartbeat re-alerts every 10 seconds while a high-confidence device remains in range.

### TelemetryReporter (`common/TelemetryReporter.h`)

Serializes `ThreatEvent` as JSON via ArduinoJson (`StaticJsonDocument<512>`). See [Telemetry Format](telemetry-format.md) for the schema.

### DeviceSignatures (`common/DeviceSignatures.h`)

Static arrays of known MAC OUI prefixes. SSID patterns and BLE identifiers are matched by detector functions in `Detectors.h` rather than by static lookup tables.

### SoundEngine (variant `src/SoundEngine.h`)

Variant-specific audio output:
- **Mini12864 / 128x32 OLED**: I2S WAV playback from LittleFS (MAX98357A amplifier)
- **M5Stack variants**: M5.Speaker tone generation or SD card WAV playback
- **128x32 Portable**: GPIO buzzer tones
- **Flipper Zero**: No audio (UART-only)

## Pluggable Detector System

Defined in `common/DetectorTypes.h` and `common/Detectors.h`.

Each detector is a plain function that takes a frame/device event and returns a `DetectorResult` (matched, weight, name). Detectors are registered in static arrays in `common/ThreatAnalyzer.h`.

### WiFi Detectors

| Function | Flag | Weight | What it matches |
|----------|------|--------|----------------|
| `detectSsidFormat` | `DET_SSID_FORMAT` | 75 | Exact patterns: `Flock-XXXXXX` (hex), `Penguin-XXXXXXXXXX` (decimal), `FS Ext Battery` |
| `detectSsidKeyword` | `DET_SSID_KEYWORD` | 45 | Case-insensitive substring: "flock", "penguin", "pigvision" |
| `detectWifiMacOui` | `DET_MAC_OUI` | 20 | MAC OUI prefix in `DeviceSignatures.h` |

### BLE Detectors

| Function | Flag | Weight | What it matches |
|----------|------|--------|----------------|
| `detectBleName` | `DET_BLE_NAME` | 55 | Case-insensitive substring in device name |
| `detectRavenCustomUuid` | `DET_RAVEN_CUSTOM_UUID` | 80 | UUID prefix `00003X00` where X is 1-5 |
| `detectRavenStdUuid` | `DET_RAVEN_STD_UUID` | 10 | Standard BLE SIG UUIDs (0x180A, 0x1809, 0x1819) |
| `detectBleMacOui` | `DET_MAC_OUI` | 20 | MAC OUI prefix in `DeviceSignatures.h` |

### RSSI Modifier

Applied to all detections after detector scoring:

| RSSI range | Modifier |
|-----------|----------|
| > -50 dBm (very close) | +10 |
| -50 to -70 dBm | 0 |
| -70 to -85 dBm | -5 |
| < -85 dBm (very weak) | -10 |

### Subsumption

When both `DET_SSID_FORMAT` and `DET_SSID_KEYWORD` match the same frame, the keyword weight is removed (format is more specific and already includes the keyword signal).

## Thread Safety

WiFi promiscuous callbacks and BLE scan callbacks run on different cores/tasks than `loop()`. All variants use the same pattern:

- `portMUX_TYPE` spinlocks guard shared volatile state in ISR callbacks
- `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` for atomic reads/writes
- Main loop copies event data under lock, then processes outside the critical section

## Source Layout

Shared headers live in `common/` and are included at compile time via `-I common` (set in the Makefile). Each variant also has its own `src/` directory for hardware-specific code (RadioScanner, SoundEngine, Display). Some variants still have local copies of shared headers in `src/` from before the `common/` migration.
