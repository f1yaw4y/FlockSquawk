# FlockSquawk

A modular, event-driven ESP32 project that passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning. Features built-in speaker audio alerts and JSON telemetry output.

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Bluetooth Low Energy**: Active scanning for device names, MAC addresses, and service UUIDs
- **Pattern Matching**: Identifies devices based on SSID patterns, MAC prefixes, device names, and service UUIDs
- **Audio Alerts**: Built-in M5Stack FIRE speaker playback with volume control
- **JSON Telemetry**: Structured event reporting via serial output
- **Event-Driven Architecture**: Modular design for easy extension

## Hardware Requirements

### Required Components

- **M5Stack FIRE IoT Development Kit (PSRAM) V2.7**
- **USB Cable** for programming and power

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
6. Select your ESP32 board: **Tools** → **Board** → **M5Stack Fire**

### Required Libraries

Install the following libraries via Arduino IDE Library Manager:

1. **M5Unified** by M5Stack
   - **Tools** → **Manage Libraries** → Search "M5Unified" → Install

2. **ArduinoJson** by Benoit Blanchon (version 6.x or 7.x)
   - **Tools** → **Manage Libraries** → Search "ArduinoJson" → Install

3. **NimBLE-Arduino** by h2zero
   - **Tools** → **Manage Libraries** → Search "NimBLE-Arduino" → Install

### Additional ESP32 Tools

The following components are included with ESP32 board support:
- WiFi (built-in)
- SD (built-in)

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

The project requires three WAV audio files on the SD card (root directory):

- `/startup.wav` - Plays on boot
- `/ready.wav` - Plays when system is ready
- `/alert.wav` - Plays on threat detection

**Audio File Requirements:**
- Format: 16-bit PCM WAV
- Sample Rate: 16000 Hz
- Channels: Mono (1 channel)
- Header: Standard 44-byte WAV header

**Adding Audio Files:**

1. Format the SD card as FAT32.
2. Copy the WAV files to the root of the SD card:
   ```
   /startup.wav
   /ready.wav
   /alert.wav
   ```
3. Insert the SD card into the M5Stack FIRE before powering on.

### Step 4: Configure Board Settings

1. Select your board: **Tools** → **Board** → **M5Stack Fire**
2. Set PSRAM: **Tools** → **PSRAM** → **Enabled**
3. Set upload speed: **Tools** → **Upload Speed** → **115200** (or lower if upload fails)
4. Set CPU frequency: **Tools** → **CPU Frequency** → **240MHz (WiFi/BT)**
5. Set partition scheme: **Tools** → **Partition Scheme** → **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)** or **Huge APP (3MB No OTA/1MB SPIFFS)**

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

### Audio Alerts

- **Startup**: Plays when system boots (display shows "Startup")
- **Ready**: Plays when scanning begins (display shows "ready", then "scanning...")
- **Alert**: Plays when a threat is detected

### Volume Control

Default volume is set to 40% (0.4). To adjust:

1. Open `src/SoundEngine.h`
2. Change `DEFAULT_VOLUME` value:
   ```cpp
   static constexpr float DEFAULT_VOLUME = 0.4f;  // 0.0 to 1.0
   ```
3. Re-upload code

Or use the serial command (if implemented) to change volume at runtime.

## Configuration

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

1. **Check SD card**: Ensure audio files are on the SD card root and the card is inserted
2. **Check volume**: Try increasing `DEFAULT_VOLUME` in `SoundEngine.h`
3. **Check serial output**: Look for audio file open errors

### No Detections

1. **Check serial output**: Verify system initialized correctly
2. **Test with known device**: Use a smartphone with WiFi hotspot named "Flock"
3. **Check channel**: WiFi channel hopping may miss brief transmissions
4. **Verify patterns**: Check `DeviceSignatures.h` matches your target devices

### Compilation Errors

1. **Missing libraries**: Install M5Unified, ArduinoJson, and NimBLE-Arduino
2. **Wrong board**: Select correct ESP32 board variant
3. **Outdated ESP32 core**: Update ESP32 board support package
4. **File structure**: Ensure all `.h` files are in `src/` directory

### Upload Failures

1. **Hold BOOT button**: Hold BOOT button while clicking Upload, release after upload starts
2. **Lower upload speed**: Change to 115200 or 9600 baud
3. **Check USB cable**: Use a data cable, not charge-only
4. **Driver issues**: Install ESP32 USB drivers (CP210x or CH340)

### SD Card Not Detected

1. **Check format**: Use FAT32
2. **Check insertion**: Power off, insert card, then power on
3. **Try a smaller card**: 32GB or less is usually more reliable

## Project Structure

```
flocksquawk/
├── flocksquawk.ino             # Main orchestrator
├── README.md                   # This file
├── src/
│   ├── EventBus.h             # Event system interface
│   ├── DeviceSignatures.h     # Detection patterns
│   ├── RadioScanner.h         # RF scanning interface
│   ├── ThreatAnalyzer.h       # Detection engine interface
│   ├── SoundEngine.h          # Audio playback interface
│   └── TelemetryReporter.h    # JSON reporting interface
└── (audio files on SD card root)
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
