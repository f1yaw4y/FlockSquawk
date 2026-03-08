# FlockSquawk (Mini12864)

Full-featured variant with ST7567 LCD display, rotary encoder, menu system, and I2S audio. Passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning.

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Bluetooth Low Energy**: Active scanning for device names, MAC addresses, and service UUIDs
- **Pattern Matching**: Identifies devices based on SSID patterns, MAC prefixes, device names, and service UUIDs
- **Audio Alerts**: I2S-based audio playback with volume control (MAX98357A amplifier)
- **Mini12864 Display UI**: Boot status, scanning screen, live MAC display, radar sweep
- **Menu System**: Backlight customization and LED ring presets
- **Visual RF Activity**: Radar dots that scale with RSSI strength
- **JSON Telemetry**: Structured event reporting via serial output
- **Event-Driven Architecture**: Modular design for easy extension

## Hardware Requirements

- **ESP32 Development Board** (e.g., ESP32 DevKit, NodeMCU-32S)
- **MAX98357A I2S Audio Amplifier Module**
- **Speaker** (4-8 ohm, 3-5W recommended)
- **Mini12864 128x64 Display** (ST7567)
- **Rotary Encoder with Push Button**
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

### Display + Encoder Pins

| ESP32 Pin | Display Pin | Description |
|-----------|-------------|-------------|
| GPIO 5 | CS | LCD chip select |
| GPIO 16 | RST | LCD reset |
| GPIO 17 | DC | LCD data/command |
| GPIO 23 | MOSI | LCD data |
| GPIO 18 | SCK | LCD clock |

| ESP32 Pin | Encoder Pin | Description |
|-----------|-------------|-------------|
| GPIO 22 | A | Encoder A |
| GPIO 14 | B | Encoder B |
| GPIO 13 | SW | Encoder button |

### Backlight / LED Ring

- **WS2811/NeoPixel backlight (default)**: `GPIO 4` as data line
- **PWM RGB backlight**: `GPIO 32/33/4` (see `Mini12864Display.cpp`)

## Setup

For Arduino IDE installation and ESP32 board support, see [Getting Started](../../docs/getting-started.md).

### Additional Libraries

Install via Arduino IDE Library Manager:

- **U8g2** by olikraus
- **Adafruit NeoPixel** by Adafruit (optional, only if using WS2811 backlight)

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
1. Install the **ESP32 LittleFS Filesystem Uploader** plugin:
   - Download from: https://github.com/lorol/arduino-esp32fs-plugin/releases
   - Extract to: `<Arduino Sketchbook>/tools/ESP32FS/tool/esp32fs.jar`
   - Restart Arduino IDE
2. Use Ctrl+Shift+P, type "upload", select the LittleFS upload option
3. Or use **Tools** > **ESP32 Sketch Data Upload**

**Upload via Makefile:** `make upload-data-mini12864`

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
   - Play startup sound, show boot progress on the display
   - Initialize WiFi sniffer and BLE scanner
   - Play ready sound
   - Begin scanning for targets

### Display + Controls

**Home screen:**
- Shows "Scanning for Flock signatures" with a radar sweep
- Live channel indicator (`CH`) and volume (`Vol 0-10`)
- Live MAC line: most recent WiFi frame and its channel
- Radar dots appear for each captured WiFi frame; dot size scales with RSSI strength

**Encoder controls:**
- Turn on Home screen to adjust volume (0.0-1.0 in steps of 0.1)
- Press to open Menu

**Menu:**
- `Backlight` -- adjust Display RGB or LED Ring presets/custom RGB
- `Test Alert` (temporary) -- plays alert and shows alert screen
- `Back` -- returns to Home

**Alert screen:**
- Flashes "ALERT" with red backlight
- Auto-exits after ~10 seconds
- Press encoder button to dismiss early

### Volume Control

Default volume is 40% (0.4). Adjustable at runtime via the encoder, or edit `src/SoundEngine.h`:

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

### Filesystem Upload Fails

1. **Check partition scheme**: Use partition scheme with SPIFFS/LittleFS
2. **Check file sizes**: Ensure total data fits in filesystem partition
3. **Close serial monitors**: Uploader needs exclusive port access
4. **Restart Arduino IDE** and retry

### Display Not Working

1. Verify SPI wiring (CS, RST, DC, MOSI, SCK)
2. Check that the correct constructor is used in `Mini12864Display.h` for your display variant

## Project Structure

```
flocksquawk_mini12864/
├── flocksquawk_mini12864.ino   # Main orchestrator
├── README.md
├── src/
│   ├── Mini12864Display.h       # Display and menu interface
│   ├── Mini12864Display.cpp     # Display implementation
│   ├── RadioScanner.h           # Variant-specific RF scanning
│   └── SoundEngine.h            # I2S audio playback
└── data/
    ├── startup.wav
    ├── ready.wav
    └── alert.wav
```

Shared headers (`EventBus.h`, `ThreatAnalyzer.h`, `Detectors.h`, etc.) are in [`common/`](../../common/).

## Further Reading

- [Configuration](../../docs/configuration.md) -- WiFi/BLE tuning, detection patterns, display settings
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
