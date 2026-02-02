# Extending FlockSquawk

## Adding Detection Patterns

### New MAC OUI Prefixes

Edit `common/DeviceSignatures.h` and add entries to the `MACPrefixes` array:

```cpp
const char* const MACPrefixes[] = {
    "58:8e:81", "cc:cc:cc",
    "aa:bb:cc",  // your new OUI
};
```

The `MACPrefixCount` is computed automatically from the array size.

### New SSID Keywords

Edit `common/Detectors.h`, find the `detectSsidKeyword` function, and add to the `keywords[]` array:

```cpp
static const char* const keywords[] = {
    "flock", "penguin", "pigvision",
    "yournewkeyword",  // add here
};
```

### New SSID Format Patterns

Add a new branch in `detectSsidFormat()` in `common/Detectors.h`:

```cpp
if (len == expectedLen && strncmp(ssid, "Prefix-", 7) == 0 &&
    isHexSuffix(ssid + 7, expectedLen - 7)) {
    r.matched = true;
    return r;
}
```

### New BLE Device Names

Edit `detectBleName()` in `common/Detectors.h` and add to the `names[]` array.

## Writing a New Detector

Detectors are plain functions registered in `common/ThreatAnalyzer.h`. To add a new one:

1. **Define the function** in `common/Detectors.h`:

```cpp
inline DetectorResult detectMyNewSignal(const WiFiFrameEvent& frame) {
    DetectorResult r = { false, 50, "my_new_signal" };
    // your matching logic here
    if (/* match condition */) r.matched = true;
    return r;
}
```

2. **Add a flag bit** in `common/DetectorTypes.h`:

```cpp
DET_MY_NEW_SIGNAL = (1 << 7),  // next available bit
```

3. **Register it** in the `wifiDetectors[]` or `bleDetectors[]` array in `common/ThreatAnalyzer.h`:

```cpp
static const WiFiDetectorEntry wifiDetectors[] = {
    { detectSsidFormat,    DET_SSID_FORMAT  },
    { detectSsidKeyword,   DET_SSID_KEYWORD },
    { detectWifiMacOui,    DET_MAC_OUI      },
    { detectMyNewSignal,   DET_MY_NEW_SIGNAL },  // add here
};
```

4. **Update TelemetryReporter** if you want the new detector name in JSON output. Add the name string to the `detectorNames[]` array in `common/TelemetryReporter.h` at the bit position matching your flag.

## Adding New EventBus Subscribers

Subscribe to events in `setup()` of the variant's `.ino` file:

```cpp
// React to threat detections
EventBus::subscribeThreat([](const ThreatEvent& event) {
    // your code here -- update display, toggle GPIO, send network message, etc.
});

// React to raw WiFi frames
EventBus::subscribeWifiFrame([](const WiFiFrameEvent& frame) {
    // your code here
});

// React to BLE advertisements
EventBus::subscribeBluetoothDevice([](const BluetoothDeviceEvent& device) {
    // your code here
});
```

## Adding a New Hardware Variant

1. Create a new directory following the existing convention (e.g., `myboard/flocksquawk_myboard/`)
2. Create the main `.ino` sketch file
3. Add a `src/` subdirectory with at minimum:
   - `RadioScanner.h` -- hardware-specific WiFi/BLE initialization
   - `SoundEngine.h` -- audio output (or stub if none)
   - Any display driver code
4. Shared headers (`EventBus.h`, `ThreatAnalyzer.h`, `Detectors.h`, etc.) are included from `common/` via the Makefile's `-I common` flag. If building via the Arduino IDE without the Makefile, copy the shared headers into your `src/` directory.
5. Add your variant to the Makefile:
   ```makefile
   myboard_FQBN   := esp32:esp32:your_fqbn
   myboard_SKETCH := myboard/flocksquawk_myboard
   myboard_DATA   :=  # set to 1 if your variant has a data/ directory
   ```
   And add `myboard` to the `VARIANTS` list.

## Syncing Changes Across Variants

Shared logic lives in `common/`. Changes to files there apply to all variants at build time (when using the Makefile). Some variants still have local copies of shared headers in their `src/` directories from before the `common/` migration. When modifying shared logic:

1. Edit the file in `common/`
2. Check if any variant has a local override in its `src/` -- if so, either update it too or remove it so the variant picks up the shared version
3. Run `make all` or `make test` to verify nothing broke
