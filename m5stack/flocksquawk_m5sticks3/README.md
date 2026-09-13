# FlockSquawk (M5StickS3)

A compact handheld variant with built-in display and speaker alerts. Passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning.

This is the ESP32-S3 sibling of the [M5StickC Plus2 variant](../flocksquawk_m5stick/README.md). The detection pipeline and UI are identical; the differences are the audio path, the battery readout, and the board settings.

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Bluetooth Low Energy**: Active scanning for device names, MAC addresses, and service UUIDs
- **Pattern Matching**: Identifies devices based on SSID patterns, MAC prefixes, device names, and service UUIDs
- **Speaker Alerts**: Tones for startup and detections through the onboard speaker
- **Status Display**: Scanning state, current WiFi channel, RSSI chart, detection count
- **JSON Telemetry**: Structured event reporting via serial output
- **Event-Driven Architecture**: Modular design for easy extension

## Hardware Requirements

- **M5StickS3** (ESP32-S3-PICO-1-N8R8, 8MB flash / 8MB OPI PSRAM, SKU K150)
- **USB-C Cable** for programming and power

No external components are needed.

## Setup

For Arduino IDE installation and ESP32 board support, see [Getting Started](../../docs/getting-started.md).

### Additional Libraries

Install via Arduino IDE Library Manager:

- **M5Unified** by M5Stack -- **0.2.12 or newer** (StickS3 board support was added in 0.2.12; 0.2.13 fixed its audio path)
- **NimBLE-Arduino**
- **ArduinoJson**

### Board Settings

ESP32 core 3.0.7 has no dedicated StickS3 board definition, so the board is built as a generic ESP32-S3. M5Unified identifies the hardware at runtime rather than from the board selection, so this works correctly.

1. Select board: **Tools** > **Board** > **ESP32 Arduino** > **ESP32S3 Dev Module**
2. **PSRAM**: `OPI PSRAM` -- required; the StickS3 has 8MB of OPI PSRAM and will not boot reliably with this wrong
3. **Flash Size**: `8MB (64Mb)`
4. **Partition Scheme**: `8M with spiffs (3MB APP/1.5MB SPIFFS)`
5. **USB CDC On Boot**: `Enabled` -- required, or the Serial Monitor stays blank
6. **CPU Frequency**: `240MHz (WiFi/BT)`

The equivalent FQBN, as used by the Makefile:

```
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc
```

### Upload

1. Connect via USB-C
2. Select port: **Tools** > **Port**
3. Click **Upload**

If the upload cannot connect, hold the **BOOT/G0** button while powering on to force download mode.

### Serial Monitor

Open at **115200** baud. See [Telemetry Format](../../docs/telemetry-format.md) for the JSON schema.

## Usage

1. Power on the StickS3
2. The system will:
   - Show `Flock Detector V1.0` and `Starting...` with short beeps
   - Initialize the WiFi sniffer and BLE scanner
   - Begin scanning, showing the current WiFi channel, an RSSI chart and a detection count

### Controls

- **BtnA** (front): wake the display while power saving is active
- **BtnB** (side), held 2s: toggle power saver

### Speaker Alerts

- **Startup**: three short beeps
- **Alert**: repeated tones while the detection banner flashes

## Differences from the M5StickC Plus2 Variant

| Area | StickC Plus2 | StickS3 |
|------|--------------|---------|
| Audio | Passive buzzer, fixed volume | ES8311 codec + AW8737 amp; volume, magnification and tone length all set explicitly |
| Alert LED | Red LED flashes with the alert | No user LED; `setAlertLed()` is a deliberate no-op |
| Battery | Read from AXP192 | Read via M5Unified; shows `--` if the gauge is unavailable |
| Backlight | Set by `M5.begin()` | Brightness reapplied after `M5.begin()` |
| Board | `m5stack_stickc_plus2` | Generic ESP32-S3 with OPI PSRAM |

### Audio notes

Two things about this board's audio path are worth knowing before changing the
tone constants, both measured on hardware:

- **Magnification.** M5Unified defaults the StickS3 to `magnification = 1`,
  which through the codec and amplifier is inaudible. Values 1 through 12 were
  all verified to play cleanly on USB power; 16 triggered the ESP32 brownout
  detector while running from a partly discharged battery. `SPEAKER_MAGNIFICATION`
  is set to **8** — audible in a moving car, with headroom left for the radios,
  which are transmitting at the moment an alert fires.
- **Tone length.** The ES8311 takes a moment to wake and unmute at the start of
  playback. The Plus2's 80ms beeps are mostly consumed by that latency here and
  sound like silence, so tone durations are longer in this variant. Anything
  under roughly 90ms is unreliable.

### Alert tone

The alert is a two-tone klaxon (520/700Hz, alternating every 190ms) rather than
the single repeated 2600Hz beep the other variants use. Chosen by ear against
eight alternatives, on the reasoning that a car cabin absorbs high frequencies:
a 2600Hz beep tests well on a desk and vanishes at speed. The tone runs on its
own timer rather than riding the screen-flash interval, so its cadence can be
tuned without changing the blink rate.

### Battery life

The StickS3 carries a 250mAh cell, and WiFi promiscuous mode plus BLE scanning
draws continuously. Expect on the order of an hour or two of untethered
runtime. The power saver only blanks the screen; the radios dominate
consumption, so it extends runtime less than you might expect. For sustained
use, run it from USB.

Note also that the battery percentage reads high for the first minute or so
after unplugging, as the cell sheds surface charge. A rapid apparent drop right
after disconnecting the charger is the gauge catching up, not a fault.

## Project Structure

```
flocksquawk_m5sticks3/
├── flocksquawk_m5sticks3.ino   # Main orchestrator
├── README.md
└── src/
    └── RadioScanner.h           # Variant-specific RF scanning
```

Shared headers (`EventBus.h`, `ThreatAnalyzer.h`, `Detectors.h`, etc.) are in [`common/`](../../common/).

## Further Reading

- [Configuration](../../docs/configuration.md) -- WiFi/BLE tuning, detection patterns
- [Architecture](../../docs/architecture.md) -- pipeline, detectors, thread safety
- [Extending](../../docs/extending.md) -- adding detectors, patterns, new variants
- [Build System](../../docs/build-system.md) -- Makefile and Docker builds
- [Troubleshooting](../../docs/troubleshooting.md) -- common issues

## License

[GNU GENERAL PUBLIC LICENSE](https://github.com/f1yaw4y/FlockSquawk/blob/main/LICENSE)

## Acknowledgments

- Inspired by [flock-you](https://github.com/colonelpanichacks/flock-you)
- ESP32 community for excellent hardware support
- NimBLE-Arduino for efficient BLE scanning
- ArduinoJson for flexible JSON handling
