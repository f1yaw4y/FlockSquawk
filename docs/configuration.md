# Configuration

Runtime behavior is controlled by constants in each variant's `src/` headers. After changing a value, recompile and re-upload.

## WiFi Channel Hopping

Edit `src/RadioScanner.h` in your variant:

```cpp
static const uint8_t MAX_WIFI_CHANNEL = 13;
static const uint16_t CHANNEL_SWITCH_MS = 500;
```

- `MAX_WIFI_CHANNEL` -- highest channel to scan (1-13, or 1-11 for US-only)
- `CHANNEL_SWITCH_MS` -- dwell time per channel in milliseconds (500 in most variants, 1000 in M5Stick)

## BLE Scan Interval

Edit `src/RadioScanner.h` in your variant:

```cpp
static const uint8_t BLE_SCAN_SECONDS = 1;
static const uint32_t BLE_SCAN_INTERVAL_MS = 5000;
```

- `BLE_SCAN_SECONDS` -- active scan duration per cycle
- `BLE_SCAN_INTERVAL_MS` -- time between scan cycles

Not applicable to the Flipper Zero variant (ESP32-S2 has no BLE).

## Detection Patterns

Detection logic is implemented in `common/Detectors.h`. To add or modify what the system detects:

### MAC OUI Prefixes

Edit `common/DeviceSignatures.h`:

```cpp
const char* const MACPrefixes[] = {
    "58:8e:81", "cc:cc:cc",
    "your:ne:wp",  // add here
};
```

### SSID Patterns

Modify the detector functions in `common/Detectors.h`:

- **Exact format patterns** (`detectSsidFormat`) -- add new prefix+suffix rules
- **Keyword substrings** (`detectSsidKeyword`) -- add entries to the `keywords[]` array
- **BLE device names** (`detectBleName`) -- add entries to the `names[]` array

See [Architecture: Pluggable Detector System](architecture.md#pluggable-detector-system) for how detectors work.

## Audio Volume

Variants with I2S audio or M5Stack speaker can adjust volume in `src/SoundEngine.h`:

```cpp
static constexpr float DEFAULT_VOLUME = 0.4f;  // 0.0 to 1.0
```

The Mini12864 variant also supports runtime volume adjustment via the rotary encoder.

## Display-Specific Settings

### Mini12864

Edit `src/Mini12864Display.cpp`:

```cpp
// Startup backlight color timing
static const uint32_t STARTUP_RED_MS = 1000;
static const uint32_t STARTUP_GREEN_MS = 1000;
static const uint32_t STARTUP_BLUE_MS = 1000;
static const uint32_t STARTUP_NEO_MS = 1000;

// Radar visualization
static const uint16_t RADAR_DOT_TTL_MS = 8000;
static const uint8_t RADAR_DOT_STEP = 3;
static const uint8_t RADAR_DOT_MAX = 40;
```
