# FlockSquawk (128x32 OLED)

128x32 I2C OLED variant with I2S audio playback. Passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning.

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Bluetooth Low Energy**: Active scanning for device names, MAC addresses, and service UUIDs
- **Pattern Matching**: Identifies devices based on SSID patterns, MAC prefixes, device names, and service UUIDs
- **Audio Alerts**: I2S-based audio playback with volume control (MAX98357A amplifier)
- **128x32 OLED Display**: Compact status display over I2C
- **JSON Telemetry**: Structured event reporting via serial output
- **Event-Driven Architecture**: Modular design for easy extension

## Hardware Requirements

- **ESP32 Development Board** (e.g., ESP32 DevKit, NodeMCU-32S)
- **MAX98357A I2S Audio Amplifier Module**
- **Speaker** (4-8 ohm, 3-5W recommended)
- **128x32 I2C OLED Display** (SSD1306/SH1106 compatible)
- **USB Cable** for programming and power
- **Breadboard and jumper wires** (optional, for prototyping)

### I2S Audio Wiring

| ESP32 Pin | MAX98357A Pin | Description |
|-----------|---------------|-------------|
| GPIO 27 | BCLK | Bit Clock |
| GPIO 26 | LRC | Left/Right Clock (Word Select) |
| GPIO 25 | DIN | Data Input |
| 5V | VIN | Power (5V) |
| GND | GND | Ground |

**Speaker:** MAX98357A `OUT+` > Speaker (+), `OUT-` > Speaker (-)

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

### Audio Files

The project requires three WAV audio files in the `data/` folder, uploaded to LittleFS:

- `data/startup.wav` -- Plays on boot
- `data/ready.wav` -- Plays when system is ready
- `data/alert.wav` -- Plays on threat detection

**Audio File Requirements:**
- Format: 16-bit PCM WAV
- Sample Rate: 16000 Hz
- Channels: Mono (1 channel)
- Header: Standard 44-byte WAV header

**Upload via Arduino IDE:**
1. Install the **ESP32 LittleFS Filesystem Uploader** plugin
2. Use Ctrl+Shift+P, type "upload", select the LittleFS upload option
3. Or use **Tools** > **ESP32 Sketch Data Upload**

**Upload via Makefile:** `make upload-data-oled`

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
   - Initialize filesystem and audio
   - Play startup sound
   - Initialize WiFi sniffer and BLE scanner
   - Play ready sound
   - Begin scanning for targets

### Volume Control

Default volume is 40% (0.4). To adjust, edit `src/SoundEngine.h`:

```cpp
static constexpr float DEFAULT_VOLUME = 0.4f;  // 0.0 to 1.0
```

## Variant-Specific Troubleshooting

### Audio Not Playing

1. **Check wiring**: Verify all I2S connections
2. **Check filesystem**: Ensure audio files are uploaded via LittleFS uploader
3. **Check volume**: Try increasing `DEFAULT_VOLUME` in `SoundEngine.h`
4. **Check speaker**: Test speaker with another device
5. **Check serial output**: Look for audio file open errors

### OLED Screen Blank

1. Verify VCC is 3.3V (not 5V)
2. Confirm SDA = GPIO21, SCL = GPIO22
3. Run an I2C scanner sketch to confirm address
4. Try both `0x3C` and `0x3D`

### Filesystem Upload Fails

1. **Check partition scheme**: Use partition scheme with SPIFFS/LittleFS
2. **Close serial monitors**: Uploader needs exclusive port access
3. **Restart Arduino IDE** and retry

## Project Structure

```
flocksquawk_128x32/
├── flocksquawk_128x32.ino      # Main orchestrator
├── README.md
├── src/
│   ├── DisplayEngine.h          # OLED display driver
│   ├── RadioScanner.h           # Variant-specific RF scanning
│   └── SoundEngine.h            # I2S audio playback
└── data/
    ├── startup.wav
    ├── ready.wav
    └── alert.wav
```

Shared headers (`EventBus.h`, `ThreatAnalyzer.h`, `Detectors.h`, etc.) are in [`common/`](../../common/).

## Further Reading

- [Configuration](../../docs/configuration.md) -- WiFi/BLE tuning, detection patterns, volume
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
