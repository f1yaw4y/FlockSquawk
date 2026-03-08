# FlockSquawk (128x32 OLED Portable)

Portable variant with buzzer alerts and compact OLED display. Passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning.

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Bluetooth Low Energy**: Active scanning for device names, MAC addresses, and service UUIDs
- **Pattern Matching**: Identifies devices based on SSID patterns, MAC prefixes, device names, and service UUIDs
- **Buzzer Alerts**: Simple tones on a GPIO pin
- **128x32 OLED Display UI**: Compact status output over I2C
- **JSON Telemetry**: Structured event reporting via serial output
- **Event-Driven Architecture**: Modular design for easy extension

## Hardware Requirements

- **ESP32 Development Board** (e.g., ESP32 DevKit, NodeMCU-32S)
- **Piezo Buzzer** (active or passive)
- **128x32 I2C OLED Display** (SSD1306 / SH1106 compatible)
- **USB Cable** for programming and power
- **Breadboard and jumper wires** (optional, for prototyping)

Typical display listings: "0.91 inch 128x32 I2C OLED SSD1306"

### Buzzer Wiring

| ESP32 Pin | Buzzer Pin | Description |
|-----------|------------|-------------|
| GPIO 23 | + / SIG | Buzzer signal |
| GND | - | Ground |

If your wiring uses a different GPIO, update `kBuzzerPin` in `flocksquawk_128x32_portable.ino`.

### OLED Wiring (I2C)

| OLED Pin | ESP32 Pin | Notes |
|----------|-----------|-------|
| VCC | 3.3V | Do not use 5V |
| GND | GND | Common ground |
| SDA | GPIO 21 | Default ESP32 I2C SDA |
| SCL | GPIO 22 | Default ESP32 I2C SCL |

I2C address: most modules use `0x3C` (some use `0x3D`).

## Setup

For Arduino IDE installation and ESP32 board support, see [Getting Started](../../docs/getting-started.md).

### Additional Libraries

Install via Arduino IDE Library Manager:

- **Adafruit GFX Library** by Adafruit
- **Adafruit SSD1306** by Adafruit

### Board Settings

1. Select board: **Tools** > **Board** > **ESP32 Dev Module**
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

1. Power on the ESP32
2. The system will:
   - Initialize WiFi sniffer and BLE scanner
   - Emit buzzer tones on startup/ready/alerts
   - Begin scanning for targets
3. Status information is shown on the 128x32 OLED

## Variant-Specific Troubleshooting

### OLED Screen Blank

1. Verify VCC is 3.3V (not 5V)
2. Confirm SDA = GPIO21, SCL = GPIO22
3. Run an I2C scanner sketch to confirm address
4. Try both `0x3C` and `0x3D`

## Project Structure

```
flocksquawk_128x32_portable/
├── flocksquawk_128x32_portable.ino   # Main orchestrator
├── README.md
└── src/
    └── RadioScanner.h                 # Variant-specific RF scanning
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
