# FlockSquawk (128x32 OLED Variant)

A modular, event-driven ESP32 project that passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning. Features audio alerts via I2S playback and JSON telemetry output.

This README applies **only to the 128x32 I2C OLED version** of FlockSquawk.

---

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Bluetooth Low Energy**: Active scanning for device names, MAC addresses, and service UUIDs
- **Pattern Matching**: Identifies devices based on SSID patterns, MAC prefixes, device names, and service UUIDs
- **Audio Alerts**: I2S-based audio playback with volume control (MAX98357A amplifier)
- **128x32 OLED Display UI**: Compact status output over I2C
- **JSON Telemetry**: Structured event reporting via serial output
- **Event-Driven Architecture**: Modular design for easy extension

---

## Hardware Requirements

### Required Components

- **ESP32 Development Board** (e.g., ESP32 DevKit, NodeMCU-32S)
- **MAX98357A I2S Audio Amplifier Module**
- **Speaker** (4-8Ω, 3-5W recommended)
- **128x32 I2C OLED Display** (SSD1306 / SH1106 compatible)
- **USB Cable** for programming and power
- **Breadboard and jumper wires** (optional, for prototyping)

Typical display listings:
> "0.91 inch 128x32 I2C OLED SSD1306"

Supported controllers in most cases:
- SSD1306 (most common)
- SH1106 (usually compatible)
- SSD1315

---

## Pin Connections

### Audio (MAX98357A)

| ESP32 Pin | MAX98357A Pin | Description |
|-----------|---------------|-------------|
| GPIO 27   | BCLK          | Bit Clock    |
| GPIO 26   | LRC           | Left/Right Clock (Word Select) |
| GPIO 25   | DIN           | Data Input   |
| 5V        | VIN           | Power (5V)   |
| GND       | GND           | Ground       |

**Speaker Connection:**
- MAX98357A `OUT+` → Speaker positive terminal
- MAX98357A `OUT-` → Speaker negative terminal

---

## OLED Wiring (I2C)

Most 128x32 OLED modules only require four wires:

| OLED Pin | ESP32 Pin | Notes |
|---------|----------|------|
| VCC | 3.3V | Do not use 5V |
| GND | GND | Common ground |
| SDA | GPIO 21 | Default ESP32 I2C SDA |
| SCL | GPIO 22 | Default ESP32 I2C SCL |

### I2C Address

Most modules use:
- `0x3C`

Some use:
- `0x3D`

If the display stays blank, upload an I2C scanner sketch to confirm the address.

---

## Software Setup

### Prerequisites

1. **Arduino IDE** (version 1.8.19 or later, or Arduino IDE 2.x)
2. **ESP32 Board Support** installed in Arduino IDE

### Installing ESP32 Board Support

1. Open Arduino IDE
2. Go to **File** → **Preferences**
3. In "Additional Boards Manager URLs", add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to **Tools** → **Board** → **Boards Manager**
5. Search for "ESP32" and install "esp32 by Espressif Systems"
6. Select your ESP32 board: **Tools** → **Board** → **ESP32 Arduino** → **ESP32 Dev Module** (or your specific board)

---

## Required Libraries

Install the following libraries via Arduino IDE Library Manager:

1. **ArduinoJson** by Benoit Blanchon (version 6.x or 7.x)
2. **NimBLE-Arduino** by h2zero
3. **U8g2** by olikraus (used for the 128x32 OLED)

No NeoPixel, encoder, or Mini12864 libraries are required for this variant.

---

## Installation from GitHub

### Step 1: Clone or Download Repository

```bash
git clone <repository-url>
cd flocksquawk_128x32
```

Or download as ZIP and extract.

### Step 2: Open Project in Arduino IDE

1. Open Arduino IDE
2. Navigate to **File** → **Open**
3. Select `flocksquawk.ino` from this folder

---

## Audio Files (LittleFS)

The project requires three WAV audio files in the `data` folder:

- `/data/startup.wav` - Plays on boot
- `/data/ready.wav` - Plays when system is ready
- `/data/alert.wav` - Plays on threat detection

**Audio File Requirements:**
- Format: 16-bit PCM WAV
- Sample Rate: 16000 Hz
- Channels: Mono (1 channel)
- Header: Standard 44-byte WAV header

### Uploading Audio Files

1. Place your WAV files in the `data/` directory:
   ```
   flocksquawk_128x32/
   ├── flocksquawk.ino
   └── data/
       ├── startup.wav
       ├── ready.wav
       └── alert.wav
   ```

2. Install the **ESP32 LittleFS Filesystem Uploader** plugin:
   - Download: https://github.com/lorol/arduino-esp32fs-plugin/releases
   - Extract to: `<Arduino Sketchbook>/tools/ESP32FS/tool/esp32fs.jar`
   - Restart Arduino IDE

3. Upload filesystem:
   - **Tools** → **ESP32 Sketch Data Upload**
   - Wait for upload to complete

---

## Usage

### Basic Operation

1. Power on the ESP32
2. The system will:
   - Initialize filesystem and audio
   - Play startup sound
   - Initialize WiFi sniffer and BLE scanner
   - Play ready sound
   - Begin scanning for targets
3. Status information is shown on the 128x32 OLED

### Serial Output

The system outputs JSON telemetry when threats are detected:

```json
{
  "event": "target_detected",
  "ms_since_boot": 15234,
  "source": {
    "radio": "wifi",
    "channel": 6,
    "rssi": -67
  }
}
```

---

## Configuration

### WiFi Channel Hopping

Default: Channels 1-13, switching every 500ms

Edit `src/RadioScanner.h`:
```cpp
static const uint8_t MAX_WIFI_CHANNEL = 13;
static const uint16_t CHANNEL_SWITCH_MS = 500;
```

### BLE Scan Interval

Default: 1 second scan every 5 seconds

Edit `src/RadioScanner.h`:
```cpp
static const uint8_t BLE_SCAN_SECONDS = 1;
static const uint32_t BLE_SCAN_INTERVAL_MS = 5000;
```

### Detection Patterns

Edit `src/DeviceSignatures.h` to customize detection rules:
- Network SSID names
- MAC address prefixes (OUI)
- Bluetooth device names
- Service UUIDs

---

## Troubleshooting

### OLED screen blank

1. Verify VCC is 3.3V (not 5V)
2. Confirm SDA = GPIO21, SCL = GPIO22
3. Run an I2C scanner sketch to confirm address
4. Try both `0x3C` and `0x3D` if configurable in code

### Audio Not Playing

1. Check wiring: Verify all I2S connections
2. Ensure audio files are uploaded via "ESP32 Sketch Data Upload"
3. Try increasing `DEFAULT_VOLUME` in `SoundEngine.h`
4. Check serial output for audio file errors

### Upload Failures

1. Hold BOOT button while clicking Upload
2. Lower upload speed to 115200
3. Use a known good USB data cable
4. Install correct USB driver (CP210x or CH340)

---

## Project Structure

```
flocksquawk_128x32/
├── flocksquawk_128x32.ino   # Main orchestrator
├── README.md                # This file
├── src/
│   ├── EventBus.h
│   ├── DeviceSignatures.h
│   ├── RadioScanner.h
│   ├── ThreatAnalyzer.h
│   ├── SoundEngine.h
│   └── TelemetryReporter.h
└── data/
    ├── startup.wav
    ├── ready.wav
    └── alert.wav
```

---

## Architecture

The system uses an event-driven architecture:

```
RadioScannerManager → WiFi/Bluetooth Events → EventBus
                                                 ↓
                                          ThreatAnalyzer
                                                 ↓
                                         Threat Events
                                                 ↓
                    ┌──────────────────────────┴──────────────────┐
                    ↓                                              ↓
           TelemetryReporter                                  SoundEngine
                    ↓                                              ↓
              JSON Output                                    Audio Alert
```

---

## License

[GNU GENERAL PUBLIC LICENSE](https://github.com/f1yaw4y/FlockSquawk/blob/main/LICENSE)

---

## Acknowledgments

- Inspired by [flock-you](https://github.com/colonelpanichacks/flock-you)
- ESP32 community for excellent hardware support
- NimBLE-Arduino for efficient BLE scanning
- ArduinoJson for flexible JSON handling

