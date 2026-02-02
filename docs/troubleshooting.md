# Troubleshooting

Common issues and solutions across all variants. For variant-specific problems (display, audio hardware, SD card), see the variant README.

## Compilation Errors

### IRAM overflow / linker errors

**Cause:** ESP32 board package is too new.

**Fix:** Install **esp32 by Espressif Systems** version **3.0.7 or older** via Boards Manager.

### Missing libraries

**Fix:** Install required libraries via **Tools** > **Manage Libraries**:
- ArduinoJson
- NimBLE-Arduino
- Variant-specific libraries (M5Unified, U8g2, Adafruit SSD1306, etc.)

See [Getting Started](getting-started.md) for the shared library list and your variant's README for additional requirements.

### Wrong board selected

**Fix:** Select the correct board in **Tools** > **Board**. Each variant uses a different FQBN:

| Variant | Board Selection |
|---------|----------------|
| M5StickC Plus2 | M5StickC Plus2 |
| M5Stack FIRE | M5Stack Fire |
| Mini12864 / OLED / Portable | ESP32 Dev Module |
| Flipper Zero | ESP32S2 Dev Module |

### File structure errors

Ensure all `.h` files are in the variant's `src/` directory. The Arduino IDE expects sketch-local headers to be in `src/` relative to the `.ino` file.

## Upload Failures

1. **Hold BOOT button** while clicking Upload, release after upload starts
2. **Lower upload speed** to 115200 or 9600 baud in **Tools** > **Upload Speed**
3. **Use a data cable**, not a charge-only USB cable
4. **Install USB drivers**: CP210x (Silicon Labs) or CH340 depending on your board

## No Detections

1. **Check serial output** at 115200 baud -- verify the system initialized without errors
2. **Test with a known device**: Create a WiFi hotspot named "Flock" on a smartphone
3. **Check channel timing**: Brief transmissions may be missed between hops. Try increasing `CHANNEL_SWITCH_MS` in `src/RadioScanner.h`
4. **Verify detection patterns**: Check `common/DeviceSignatures.h` and `common/Detectors.h` match your target devices

## Filesystem Upload Fails (LittleFS variants)

Applies to Mini12864, 128x32 OLED, and M5Stack FIRE variants that use audio files.

1. **Check partition scheme**: Select a scheme with SPIFFS/LittleFS space
2. **Check file sizes**: Total data must fit in the filesystem partition
3. **Close serial monitors**: The filesystem uploader needs exclusive port access
4. **Restart Arduino IDE** and retry
5. **Alternative**: Use `make upload-data-<variant>` from the [build system](build-system.md)
