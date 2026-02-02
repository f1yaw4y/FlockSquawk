# FlockSquawk (Flipper Zero WiFi Dev Board)

ESP32-S2 firmware for the official Flipper Zero WiFi Dev Board. The ESP32-S2 handles RF scanning and outputs line-based UART messages to a companion Flipper Zero app (`flock_scanner.fap`).

> Note: ESP32-S2 does not support Bluetooth/BLE, so BLE scanning is disabled.

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Pattern Matching**: Identifies devices based on SSID patterns and MAC OUIs
- **UART Telemetry**: Line-based serial protocol for the Flipper app
- **Event-Driven Architecture**: Modular design for easy extension

## Hardware Requirements

- **Flipper Zero**
- **Official Flipper Zero WiFi Dev Board (ESP32-S2)**
- **USB-C cable** (for flashing the ESP32-S2)

### UART Pins (ESP32-S2)

The WiFi Dev Board routes ESP32-S2 UART0 to the Flipper GPIO header:
- **TX** = GPIO43
- **RX** = GPIO44

These are wired on the official board; no extra wiring is required.

## Setup

For Arduino IDE installation and ESP32 board support, see [Getting Started](../docs/getting-started.md).

### Libraries

No additional libraries are required for the ESP32-S2 build (NimBLE is not used).

If your Arduino-ESP32 core does not provide `esp32-hal-neopixel.h`, install:
- **Adafruit NeoPixel** (fallback for the onboard RGB LED)

### Board Settings

1. Select board: **Tools** > **Board** > **ESP32S2 Dev Module**
2. Upload speed: **115200** (or lower if upload fails)
3. CPU frequency: **240MHz (WiFi/BT)**
4. Partition scheme: **Default 4MB with spiffs** or **Huge APP (3MB No OTA/1MB SPIFFS)**

### Upload

1. Connect ESP32-S2 via USB-C
2. Select port: **Tools** > **Port**
3. Click **Upload**

## Usage

### Basic Operation

1. Seat the WiFi Dev Board on the Flipper GPIO header
2. Power on -- the ESP32-S2 initializes WiFi scanning automatically

### UART Protocol

Line-based, newline-terminated messages (see [Telemetry Format](../docs/telemetry-format.md) for full details):

```
STATUS,SCANNING
STATUS,BLE_UNSUPPORTED
SEEN,RSSI=-62,MAC=AA:BB:CC:DD:EE:FF,CH=6
ALERT,RSSI=-62,MAC=AA:BB:CC:DD:EE:FF,RADIO=wifi,CH=6,ID=Flock,CERTAINTY=95
CLEAR
```

The Flipper app consumes these lines directly.

### RGB LED Behavior

The onboard RGB LED provides status feedback:
- Boot: cycles red/green/blue
- Scanning: flashes blue
- Alert: solid red

LED pins (discrete RGB, active-low): R=GPIO6, G=GPIO5, B=GPIO4.

## Variant-Specific Troubleshooting

### Flipper App Not Receiving UART

1. **Confirm baud rate**: 115200
2. **Confirm app**: `flipper_flock_app` installed on the Flipper
3. **Check seating**: Dev board seated properly on Flipper GPIO header
4. **Check protocol**: Use serial monitor to verify line-based output

## Project Structure

```
flipper-zero/
├── dev-board-firmware/
│   └── flocksquawk-flipper/
│       ├── flocksquawk-flipper.ino    # Main sketch
│       └── src/
│           ├── RadioScanner.h          # Variant-specific RF scanning
│           ├── SoundEngine.h           # Stub (no audio on Flipper)
│           └── TelemetryReporter.h     # UART line-based reporter
├── flock_scanner.fap                   # Flipper Zero app (prebuilt)
└── README.md
```

Shared headers (`EventBus.h`, `ThreatAnalyzer.h`, `Detectors.h`, etc.) are in [`common/`](../common/).

## Further Reading

- [Configuration](../docs/configuration.md) -- WiFi tuning, detection patterns
- [Architecture](../docs/architecture.md) -- pipeline, detectors, thread safety
- [Extending](../docs/extending.md) -- adding detectors, patterns, new variants
- [Build System](../docs/build-system.md) -- Makefile and Docker builds
- [Troubleshooting](../docs/troubleshooting.md) -- common issues

## License

[GNU GENERAL PUBLIC LICENSE](https://github.com/f1yaw4y/FlockSquawk/blob/main/LICENSE)

## Acknowledgments

- Inspired by [flock-you](https://github.com/colonelpanichacks/flock-you)
- ESP32 community for excellent hardware support
- NimBLE-Arduino for efficient BLE scanning on non-S2 targets
