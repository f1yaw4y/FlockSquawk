# Testing

FlockSquawk includes host-side unit tests that run on your development machine (no ESP32 hardware needed). Tests use the [doctest](https://github.com/doctest/doctest) framework.

## Running Tests

### Via Makefile

```bash
make test            # compile and run
make test-verbose    # run with per-assertion output
```

### Via Docker

```bash
make docker-test
make docker-test-verbose
```

### Manual

```bash
clang++ -std=c++17 -Wall -Wextra -g -O0 \
    -isystem test/mocks -I common -I test \
    test/test_main.cpp test/eventbus_impl.cpp \
    test/test_detectors.cpp test/test_device_tracker.cpp \
    test/test_threat_analyzer.cpp \
    -o .build/test_runner
.build/test_runner
```

## Test Structure

```
test/
├── test_main.cpp              # doctest entry point (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
├── eventbus_impl.cpp          # EventBus static member storage + mock millis()
├── test_detectors.cpp         # Tests for all detector functions
├── test_device_tracker.cpp    # DeviceTracker state machine tests
├── test_threat_analyzer.cpp   # ThreatAnalyzer integration tests
├── mocks/
│   └── Arduino.h              # Minimal Arduino.h mock for host compilation
└── doctest.h                  # Test framework header (auto-fetched, gitignored)
```

## What's Tested

### Detector Functions (`test_detectors.cpp`)

Tests each detector in `common/Detectors.h` individually:

- **Helper functions**: `isHexChar`, `isHexSuffix`, `isDecimalSuffix`, `ouiMatchesKnownPrefix`
- **RSSI modifier**: boundary values at -50, -70, -85 dBm
- **`detectSsidFormat`**: Flock-hex, Penguin-decimal, FS Ext Battery patterns; wrong lengths, non-hex/decimal suffixes
- **`detectSsidKeyword`**: case-insensitive keyword matching, empty/unrelated SSIDs
- **`detectWifiMacOui` / `detectBleMacOui`**: known vs unknown OUI prefixes
- **`detectBleName`**: name substring matching
- **`detectRavenCustomUuid`**: 0x3100-0x3500 range, boundary exclusion
- **`detectRavenStdUuid`**: 0x180A, 0x1809, 0x1819 matching

### Device Tracker (`test_device_tracker.cpp`)

Tests the `DeviceTracker` state machine:

- State transitions: `EMPTY` -> `NEW_DETECT` -> `IN_RANGE` -> `DEPARTED`
- Timeout behavior (60-second departure)
- High-confidence detection filtering
- Max certainty update logic
- LRU eviction when all 32 slots are full
- Preference for departed slots over active during eviction

### Threat Analyzer (`test_threat_analyzer.cpp`)

Integration tests for the full scoring pipeline:

- WiFi SSID format match produces correct certainty
- No-match produces no threat event
- Subsumption removes keyword weight when format also matches
- RSSI modifier adjusts certainty
- Certainty clamped to 0-100
- `shouldAlert` true on first detection, false on repeat
- BLE Raven UUID produces `acoustic_detector` category
- Heartbeat tick behavior

## Mock Environment

### `test/mocks/Arduino.h`

Provides minimal Arduino API stubs for host compilation:

- `millis()` -- returns `mock_millis_value` (controllable from tests)
- `constrain()` macro
- Standard C/C++ includes (`<stdint.h>`, `<cstring>`, `<functional>`)

### `test/eventbus_impl.cpp`

Provides storage for `EventBus` static members and the mock `millis()` backing variable. Handlers default to empty `std::function` (no-op).

## Adding Tests

1. Create a new `test/test_*.cpp` file
2. Include `"doctest.h"` and the header under test
3. Add the file to `TEST_SRCS` in the Makefile
4. Run `make test` to verify

## doctest.h

The test framework header is auto-fetched on first `make test` run (or pre-baked in the Docker image). It is listed in `.gitignore` and not committed to the repository.
