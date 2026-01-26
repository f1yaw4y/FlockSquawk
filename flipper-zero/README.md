# FlockSquawk (Flipper Zero WiFi Dev Board)

ESP32-S2 firmware for the official Flipper Zero WiFi Dev Board. The ESP32-S2
handles RF scanning and outputs line-based UART messages to a companion Flipper
Zero app (`flipper_flock_app/`).

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Pattern Matching**: Identifies devices based on SSID patterns and MAC OUIs
- **UART Telemetry**: Line-based serial protocol for the Flipper app
- **Event-Driven Architecture**: Modular design for easy extension

> Note: ESP32-S2 does not support Bluetooth/BLE, so BLE scanning is disabled.

## Hardware Requirements

- **Flipper Zero**
- **Official Flipper Zero WiFi Dev Board (ESP32-S2)**
- **USB-C cable** (for flashing the ESP32-S2)

### UART Pins (ESP32-S2)
The WiFi Dev Board routes ESP32-S2 UART0 to the Flipper GPIO header:
- **TX** = GPIO43
- **RX** = GPIO44

These are wired on the official board; no extra wiring is required.

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
6. Select your ESP32 board: **Tools** → **Board** → **ESP32 Arduino** → **ESP32S2 Dev Module**

### Required Libraries

No additional libraries are required for the ESP32-S2 build.

If your Arduino-ESP32 core does not provide `esp32-hal-neopixel.h`, install:
- **Adafruit NeoPixel** (used as a fallback for the onboard RGB LED)

### Additional ESP32 Tools
The following components are included with ESP32 board support:
- WiFi (built-in)

## Installation from GitHub

### Step 1: Clone or Download Repository

```bash
git clone <repository-url>
cd FlockSquawk/flipper-zero/flocksquawk-flipper
```

Or download as ZIP and extract.

### Step 2: Open Project in Arduino IDE

1. Open Arduino IDE
2. Navigate to **File** → **Open**
3. Select `flocksquawk-flipper.ino` from the `flocksquawk-flipper` folder

### Step 3: Configure Board Settings

1. Select your board: **Tools** → **Board** → **ESP32 Arduino** → **ESP32S2 Dev Module**
2. Set upload speed: **Tools** → **Upload Speed** → **115200** (or lower if upload fails)
3. Set CPU frequency: **Tools** → **CPU Frequency** → **240MHz (WiFi/BT)**
4. Set partition scheme: **Tools** → **Partition Scheme** → **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)** or **Huge APP (3MB No OTA/1MB SPIFFS)**

### Step 4: Upload Code

1. Connect ESP32 via USB
2. Select the correct port: **Tools** → **Port** → Select your ESP32 port
3. Click **Upload** button (or **Sketch** → **Upload**)
4. Wait for compilation and upload to complete

### Step 5: Monitor Serial Output

1. Open Serial Monitor: **Tools** → **Serial Monitor**
2. Set baud rate to **115200**
3. Set line ending to **Newline**
4. You should see status lines and detection events

## Usage

### Basic Operation

1. Power on the ESP32
2. The system will:
   - Initialize WiFi sniffer
   - Begin scanning for targets

### Serial Output (UART Protocol)

Line-based, newline-terminated messages:

```
STATUS,SCANNING
STATUS,BLE_UNSUPPORTED
SEEN,RSSI=-62,MAC=AA:BB:CC:DD:EE:FF,CH=6
ALERT,RSSI=-62,MAC=AA:BB:CC:DD:EE:FF,RADIO=wifi,CH=6,ID=Flock,CERTAINTY=95
CLEAR
```

The Flipper app consumes these lines directly.

### RGB LED Behavior (ESP32-S2)
The onboard RGB LED is used for quick status feedback:
- Boot: cycles red/green/blue
- Scanning: flashes blue
- Alert: solid red

The official WiFi Dev Board uses a **discrete RGB LED** (active-low) on:
- **R** = GPIO6
- **G** = GPIO5
- **B** = GPIO4

## Configuration

### WiFi Channel Hopping

Default: Channels 1-13, switching every 500ms

To modify, edit `src/RadioScanner.h`:
```cpp
static const uint8_t MAX_WIFI_CHANNEL = 13;
static const uint16_t CHANNEL_SWITCH_MS = 500;
```

### Detection Patterns

Detection patterns are defined in `src/DeviceSignatures.h`. Patterns include:
- Network SSID names
- MAC address prefixes (OUI)

> BLE identifiers remain in `DeviceSignatures.h` for non-S2 builds, but are not
> used on the ESP32-S2 WiFi Dev Board.

## Troubleshooting

### No Detections

1. **Check serial output**: Verify system initialized correctly
2. **Test with known device**: Use a smartphone with WiFi hotspot named "Flock"
3. **Check channel**: WiFi channel hopping may miss brief transmissions
4. **Verify patterns**: Check `DeviceSignatures.h` matches your target devices

### Compilation Errors

1. **Wrong board**: Select correct ESP32 board variant
2. **Outdated ESP32 core**: Update ESP32 board support package
3. **File structure**: Ensure all `.h` files are in `src/` directory

### Upload Failures

1. **Hold BOOT button**: Hold BOOT button while clicking Upload, release after upload starts
2. **Lower upload speed**: Change to 115200 or 9600 baud
3. **Check USB cable**: Use a data cable, not charge-only
4. **Driver issues**: Install ESP32 USB drivers (CP210x or CH340)

### Flipper App Not Receiving UART

1. **Confirm baud rate**: 115200
2. **Confirm app**: `flipper_flock_app` installed
3. **Check cables**: Dev board seated properly on Flipper GPIO header
4. **Check protocol**: Use serial monitor to verify line-based output

## Project Structure

```
flocksquawk-flipper/
├── flocksquawk-flipper.ino    # Main orchestrator
├── README.md                  # This file
├── flipper_flock_app/         # Flipper Zero external app
├── src/
│   ├── EventBus.h             # Event system interface
│   ├── DeviceSignatures.h     # Detection patterns
│   ├── RadioScanner.h         # RF scanning interface
│   ├── ThreatAnalyzer.h       # Detection engine interface
│   ├── SoundEngine.h          # Legacy audio (unused for Flipper)
│   └── TelemetryReporter.h    # UART reporting interface
└── data/                      # Legacy audio assets (unused)
```

## Architecture

The system uses an event-driven architecture:

```
RadioScannerManager → WiFi Events → EventBus
                                       ↓
                                ThreatAnalyzer
                                       ↓
                                 Threat Events
                                       ↓
                               TelemetryReporter
                                       ↓
                                 UART Output
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
- NimBLE-Arduino for efficient BLE scanning on non-S2 targets
