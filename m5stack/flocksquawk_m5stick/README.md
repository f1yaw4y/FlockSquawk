# FlockSquawk (M5StickC Plus2)

A compact handheld variant with built-in display and buzzer alerts. Passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning.

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Bluetooth Low Energy**: Active scanning for device names, MAC addresses, and service UUIDs
- **Pattern Matching**: Identifies devices based on SSID patterns, MAC prefixes, device names, and service UUIDs
- **Buzzer Alerts**: Internal buzzer tones for startup and detections
- **Status Display**: On-screen startup and scanning state with current WiFi channel
- **JSON Telemetry**: Structured event reporting via serial output
- **Event-Driven Architecture**: Modular design for easy extension

## Hardware Requirements

- **M5StickC PLUS2 (ESP32 V3 Mini)** device
- **USB Cable** for programming and power

## Setup

For Arduino IDE installation and ESP32 board support, see [Getting Started](../../docs/getting-started.md).

### Additional Libraries

Install via Arduino IDE Library Manager:

- **M5Unified** by M5Stack

### Board Settings

1. Select board: **Tools** > **Board** > **M5Stack** > **M5StickC Plus2**
2. Upload speed: **115200**
3. CPU frequency: **240MHz (WiFi/BT)**
4. Partition scheme: **Default 4MB with spiffs** or **Huge APP (3MB No OTA/1MB SPIFFS)**

### Upload

1. Connect via USB
2. Select port: **Tools** > **Port**
3. Click **Upload**

### Serial Monitor

Open at **115200** baud. See [Telemetry Format](../../docs/telemetry-format.md) for the JSON schema.

## Usage

1. Power on the M5StickC Plus2
2. The system will:
   - Show `starting...` on the display with short beeps
   - Initialize WiFi sniffer and BLE scanner
   - Begin scanning for targets
   - Show `Scanning` and current WiFi channel on the display

### Buzzer Alerts

- **Startup**: Short beeps when the system boots
- **Alert**: Two beeps on threat detection

## Project Structure

```
flocksquawk_m5stick/
├── flocksquawk_m5stick.ino     # Main orchestrator
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
