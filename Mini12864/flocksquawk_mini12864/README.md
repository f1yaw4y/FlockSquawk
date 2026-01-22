# FlockSquawk

A modular, event-driven ESP32 project that passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning. Features audio alerts via I2S playback and JSON telemetry output.

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

### Required Components

- **ESP32 Development Board** (e.g., ESP32 DevKit, NodeMCU-32S)
- **MAX98357A I2S Audio Amplifier Module**
- **Speaker** (4-8Ω, 3-5W recommended)
- **Mini12864 128x64 Display** (ST7567)
- **Rotary Encoder with Push Button**
- **USB Cable** for programming and power
- **Breadboard and jumper wires** (optional, for prototyping)

### Pin Connections

| ESP32 Pin | MAX98357A Pin | Description |
|-----------|---------------|-------------|
| GPIO 27   | BCLK          | Bit Clock    |
| GPIO 26   | LRC           | Left/Right Clock (Word Select) |
| GPIO 25   | DIN           | Data Input   |
| 5V        | VIN           | Power (5V)   |
| GND       | GND           | Ground       |

### Display + Encoder Pins (Mini12864 + Encoder)

| ESP32 Pin | Display Pin | Description |
|-----------|-------------|-------------|
| GPIO 5    | CS          | LCD chip select |
| GPIO 16   | RST         | LCD reset |
| GPIO 17   | DC          | LCD data/command |
| GPIO 23   | MOSI        | LCD data |
| GPIO 18   | SCK         | LCD clock |
| GPIO 19   | MISO        | Not used by display |

| ESP32 Pin | Encoder Pin | Description |
|-----------|-------------|-------------|
| GPIO 22   | A           | Encoder A |
| GPIO 14   | B           | Encoder B |
| GPIO 13   | SW          | Encoder button |

### Backlight / LED Ring

- **WS2811/NeoPixel backlight (default)**: `GPIO 4` as data line
- **PWM RGB backlight**: `GPIO 32/33/4` (see `Mini12864Display.cpp`)

**Speaker Connection:**
- MAX98357A `OUT+` → Speaker positive terminal
- MAX98357A `OUT-` → Speaker negative terminal

### Wiring Diagram

```
ESP32                    MAX98357A
------                   ---------
GPIO 27  ───────────────> BCLK
GPIO 26  ───────────────> LRC
GPIO 25  ───────────────> DIN
5V       ───────────────> VIN
GND      ───────────────> GND

MAX98357A
---------
OUT+ ────> Speaker (+)
OUT- ────> Speaker (-)
```

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

### Required Libraries

Install the following libraries via Arduino IDE Library Manager:

1. **ArduinoJson** by Benoit Blanchon (version 6.x or 7.x)
   - **Tools** → **Manage Libraries** → Search "ArduinoJson" → Install

2. **NimBLE-Arduino** by h2zero
   - **Tools** → **Manage Libraries** → Search "NimBLE-Arduino" → Install

3. **U8g2** by olikraus
   - **Tools** → **Manage Libraries** → Search "U8g2" → Install

4. **Adafruit NeoPixel** by Adafruit (optional, only if using WS2811 backlight)
   - **Tools** → **Manage Libraries** → Search "Adafruit NeoPixel" → Install

### Additional ESP32 Tools

The following components are included with ESP32 board support:
- WiFi (built-in)
- LittleFS (built-in)
- I2S driver (built-in)

## Installation from GitHub

### Step 1: Clone or Download Repository

```bash
git clone <repository-url>
cd flocksquawk
```

Or download as ZIP and extract.

### Step 2: Open Project in Arduino IDE

1. Open Arduino IDE
2. Navigate to **File** → **Open**
3. Select `flocksquawk.ino` from the `flocksquawk` folder

### Step 3: Prepare Audio Files

The project requires three WAV audio files in the `data` folder:

- `/data/startup.wav` - Plays on boot
- `/data/ready.wav` - Plays when system is ready
- `/data/alert.wav` - Plays on threat detection

**Audio File Requirements:**
- Format: 16-bit PCM WAV
- Sample Rate: 16000 Hz
- Channels: Mono (1 channel)
- Header: Standard 44-byte WAV header

**Adding Audio Files:**

1. Place your WAV files in the `data/` directory if you wish to change them:
   ```
   flocksquawk/
   ├── flocksquawk.ino
   └── data/
       ├── startup.wav
       ├── ready.wav
       └── alert.wav
   ```

2. Install the **ESP32 LittleFS Filesystem Uploader** plugin:
   - Download from: https://github.com/lorol/arduino-esp32fs-plugin/releases
   - Extract to: `<Arduino Sketchbook>/tools/ESP32FS/tool/esp32fs.jar`
   - Restart Arduino IDE

3. Write audio files to ESP32
   - Within the arduino IDE, use Ctrl + Shift + P, and type "upload"
   - Find the LittleFS upload option, and select
   - If you get an error saying unable to connect to the serial port, make sure that all serial terminals and processes are not running

4. Upload filesystem:
   - **Tools** → **ESP32 Sketch Data Upload**
   - Wait for upload to complete

### Step 4: Configure Board Settings

1. Select your board: **Tools** → **Board** → **ESP32 Dev Module**
2. Set upload speed: **Tools** → **Upload Speed** → **115200** (or lower if upload fails)
3. Set CPU frequency: **Tools** → **CPU Frequency** → **240MHz (WiFi/BT)**
4. Set partition scheme: **Tools** → **Partition Scheme** → **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)** or **Huge APP (3MB No OTA/1MB SPIFFS)**

### Step 5: Upload Code

1. Connect ESP32 via USB
2. Select the correct port: **Tools** → **Port** → Select your ESP32 port
3. Click **Upload** button (or **Sketch** → **Upload**)
4. Wait for compilation and upload to complete

### Step 6: Monitor Serial Output

1. Open Serial Monitor: **Tools** → **Serial Monitor**
2. Set baud rate to **115200**
3. Set line ending to **Newline**
4. You should see initialization messages and detection events

## Usage

### Basic Operation

1. Power on the ESP32
2. The system will:
   - Initialize filesystem and audio
   - Play startup sound
   - Show boot progress on the display
   - Initialize WiFi sniffer and BLE scanner
   - Play ready sound
   - Begin scanning for targets

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
  },
  "target": {
    "identity": {
      "mac": "aa:bb:cc:dd:ee:ff",
      "oui": "aa:bb:cc",
      "label": "Network Name"
    },
    "indicators": {
      "ssid_match": true,
      "mac_match": true,
      "name_match": false,
      "service_uuid_match": false
    },
    "category": "surveillance_device",
    "certainty": 95
  },
  "metadata": {
    "frame_type": "beacon",
    "detection_method": "combined_signature"
  }
}
```

### Display + Controls

**Home screen**
- Shows "Scanning for Flock signatures" with a radar sweep
- Live channel indicator (`CH`) and volume (`Vol 0-10`)
- Live MAC line: most recent WiFi frame and its channel
- Radar dots appear for each captured WiFi frame; dot size scales with RSSI strength

**Encoder controls**
- Turn on Home screen to adjust volume (0.0–1.0 in steps of 0.1)
- Press to open Menu

**Menu**
- `Backlight` → adjust Display RGB or LED Ring presets/custom RGB
- `Test Alert` (temporary) → plays alert and shows alert screen
- `Back` → returns to Home

**Alert screen**
- Flashes “ALERT” with red backlight
- Auto-exits after ~10 seconds
- Press encoder button to dismiss early

### Audio Alerts

- **Startup**: Plays when system boots
- **Ready**: Plays when scanning begins
- **Alert**: Plays when a threat is detected

### Volume Control

Default volume is set to 40% (0.4). To adjust at runtime:

- Use the encoder on the Home screen to set `Vol 0–10`.

To change the default:

1. Open `src/SoundEngine.h`
2. Change `DEFAULT_VOLUME` value:
   ```cpp
   static constexpr float DEFAULT_VOLUME = 0.4f;  // 0.0 to 1.0
   ```
3. Re-upload code

## Configuration
### Startup Backlight Timing

Edit `src/Mini12864Display.cpp`:
```cpp
static const uint32_t STARTUP_RED_MS = 1000;
static const uint32_t STARTUP_GREEN_MS = 1000;
static const uint32_t STARTUP_BLUE_MS = 1000;
static const uint32_t STARTUP_NEO_MS = 1000;
```

### Radar Dot Behavior

Edit `src/Mini12864Display.cpp`:
```cpp
static const uint16_t RADAR_DOT_TTL_MS = 8000;
static const uint8_t RADAR_DOT_STEP = 3;
static const uint8_t RADAR_DOT_MAX = 40;
```


### WiFi Channel Hopping

Default: Channels 1-13, switching every 500ms

To modify, edit `src/RadioScanner.h`:
```cpp
static const uint8_t MAX_WIFI_CHANNEL = 13;
static const uint16_t CHANNEL_SWITCH_MS = 500;
```

### BLE Scan Interval

Default: 1 second scan every 5 seconds

To modify, edit `src/RadioScanner.h`:
```cpp
static const uint8_t BLE_SCAN_SECONDS = 1;
static const uint32_t BLE_SCAN_INTERVAL_MS = 5000;
```

### Detection Patterns

Detection patterns are defined in `src/DeviceSignatures.h`. Patterns include:
- Network SSID names
- MAC address prefixes (OUI)
- Bluetooth device names
- Service UUIDs (e.g., Raven acoustic detectors)

## Troubleshooting

### Audio Not Playing

1. **Check wiring**: Verify all I2S connections
2. **Check filesystem**: Ensure audio files are uploaded via "ESP32 Sketch Data Upload"
3. **Check volume**: Try increasing `DEFAULT_VOLUME` in `SoundEngine.h`
4. **Check speaker**: Test speaker with another device
5. **Check serial output**: Look for audio file open errors

### No Detections

1. **Check serial output**: Verify system initialized correctly
2. **Test with known device**: Use a smartphone with WiFi hotspot named "Flock"
3. **Check channel**: WiFi channel hopping may miss brief transmissions
4. **Verify patterns**: Check `DeviceSignatures.h` matches your target devices

### Compilation Errors

1. **Missing libraries**: Install ArduinoJson and NimBLE-Arduino
2. **Wrong board**: Select correct ESP32 board variant
3. **Outdated ESP32 core**: Update ESP32 board support package
4. **File structure**: Ensure all `.h` files are in `src/` directory

### Upload Failures

1. **Hold BOOT button**: Hold BOOT button while clicking Upload, release after upload starts
2. **Lower upload speed**: Change to 115200 or 9600 baud
3. **Check USB cable**: Use a data cable, not charge-only
4. **Driver issues**: Install ESP32 USB drivers (CP210x or CH340)

### Filesystem Upload Fails

1. **Check partition scheme**: Use partition scheme with SPIFFS or LittleFS
2. **Check file sizes**: Ensure total data size fits in filesystem partition
3. **Restart IDE**: Close and reopen Arduino IDE
4. **Manual upload**: Use esptool.py or other tools to upload filesystem

## Project Structure

```
flocksquawk/
├── flocksquawk.ino          # Main orchestrator
├── README.md                   # This file
├── src/
│   ├── EventBus.h             # Event system interface
│   ├── DeviceSignatures.h     # Detection patterns
│   ├── RadioScanner.h         # RF scanning interface
│   ├── ThreatAnalyzer.h       # Detection engine interface
│   ├── SoundEngine.h          # Audio playback interface
│   ├── Mini12864Display.h      # Display and menu interface
│   └── Mini12864Display.cpp    # Display implementation
│   └── TelemetryReporter.h    # JSON reporting interface
└── data/
    ├── startup.wav            # Boot sound
    ├── ready.wav              # Ready sound
    └── alert.wav              # Alert sound
```

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

## Extending the System

### Adding New Detection Patterns

Edit `src/DeviceSignatures.h`:
```cpp
const char* const NetworkNames[] = {
    "flock",
    "YourNewPattern",  // Add here
    // ...
};
```

### Adding Display Support

Subscribe to `ThreatHandler` in `setup()`:
```cpp
EventBus::subscribeThreat([](const ThreatEvent& event) {
    display.showThreat(event);  // Your display code
});
```

### Adding LED Indicators

Subscribe to events and control GPIO:
```cpp
EventBus::subscribeThreat([](const ThreatEvent& event) {
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
});
```

## License

[GNU GENERAL PUBLIC LICENSE](https://github.com/f1yaw4y/FlockSquawk/blob/main/LICENSE)


## Acknowledgments

- Inspired by [flock-you](https://github.com/colonelpanichacks/flock-you)
- ESP32 community for excellent hardware support
- NimBLE-Arduino for efficient BLE scanning
- ArduinoJson for flexible JSON handling
